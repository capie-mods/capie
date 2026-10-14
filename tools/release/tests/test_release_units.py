"""Unit tests of the release tool's pure functions (configuration, versions, notes, signing).

Run with ``uv run pytest tools/release/tests``. No process, network or game is used.
"""

from __future__ import annotations

import io
from pathlib import Path
from typing import override

import pytest

from fakes import KeyPair
from releasekit import changelog
from releasekit.config import (
    VISIBILITY_VALUES,
    CommandSpec,
    ConfigError,
    load_config,
    set_published_file_id,
)
from releasekit.console import Reporter, Status, use_color
from releasekit.runner import expand_command, quote
from releasekit.signing import (
    PinnedRelease,
    pinned_fields,
    public_key_from_xml,
    render_pinned_block,
    replace_pinned_block,
    reviewed_hashes,
    verify_signature,
)
from releasekit.versions import (
    CapieVersions,
    capie_problems,
    descriptor_fields,
    read_capie_versions,
    stale_description_versions,
)

REPO = Path(__file__).resolve().parents[3]
"""Root of the real repository (for read-only checks against its files)."""

HIDDEN_VISIBILITY = 2
"""Steam's value for a private ("Hidden") item."""


def test_real_config_loads_and_targets_the_split_repository() -> None:
    """The shipped release.toml is valid and keeps the safe defaults."""
    config = load_config(REPO / "tools" / "release" / "release.toml")
    assert config.root == REPO
    assert config.github.repo == "capie-mods/capie"
    assert config.workshop.app_id == 394360  # noqa: PLR2004 - HOI4's Steam app ID
    assert {item.key for item in config.workshop.items} == {"rename-anything", "capie-switch"}
    assert VISIBILITY_VALUES["hidden"] == HIDDEN_VISIBILITY
    for spec in config.preflight:
        assert spec.argv, spec.name


def test_set_published_file_id_changes_only_that_item() -> None:
    """Only the named item's ID line changes; comments and other items stay."""
    text = (
        '# comment\n[[workshop.items]]\nkey = "a"\npublished_file_id = 0\n\n'
        '[[workshop.items]]\nkey = "b"\npublished_file_id = 0\n'
    )
    new = set_published_file_id(text, "b", 42)
    assert new == text.replace(
        'key = "b"\npublished_file_id = 0', 'key = "b"\npublished_file_id = 42'
    )
    with pytest.raises(ConfigError):
        set_published_file_id(text, "c", 1)


def test_expand_command_fills_or_reports_missing_placeholders() -> None:
    """A command with an unknown value is reported, never run with a hole."""
    spec = CommandSpec(
        name="gen", argv=("{python}", "gen.py", "--game", "{game_dir}"), env=(("T", "{temp}/x"),)
    )
    missing = expand_command(spec, {"python": "py", "game_dir": None, "temp": "t"})
    assert missing.missing == ("game_dir",)
    filled = expand_command(spec, {"python": "py", "game_dir": "C:/Game Dir", "temp": "t"})
    assert filled.argv == ("py", "gen.py", "--game", "C:/Game Dir")
    assert filled.env == {"T": "t/x"}
    assert quote(filled.argv) == "py gen.py --game 'C:/Game Dir'"


def test_capie_versions_come_from_game_build_hpp() -> None:
    """The real header is the single source of the CAPIE version and is consistent."""
    header = (REPO / "plugin/capie/src/core/game_build.hpp").read_text(encoding="utf-8")
    versions = read_capie_versions(header)
    assert versions.release.startswith(versions.short + ".")
    assert capie_problems(versions) == []


def test_capie_problems_catch_a_stale_build_number() -> None:
    """A release bump without the handshake number is a preflight failure."""
    versions = CapieVersions("1.5.0", "1.4", 10400, 6, "A" * 64, "1.19.3.0")
    problems = capie_problems(versions)
    assert any("capie_version" in p for p in problems)
    assert any("capie_build_number" in p for p in problems)


def test_descriptor_fields_and_stale_versions() -> None:
    """Descriptor lines are read, blocks skipped; old CAPIE versions in texts are found."""
    fields = descriptor_fields('\ufeffversion="1.0.0"\ntags={\n\t"Utilities"\n}\nname="CAPIE"\n')
    assert fields == {"version": "1.0.0", "name": "CAPIE"}
    assert stale_description_versions("CAPIE 1.3 loaded, CAPIE 1.4 and CAPIE 1.3", "1.4") == ["1.3"]


def test_changelog_section_and_bbcode() -> None:
    """A version's section is cut at the next heading and converts to Steam BBCode."""
    text = (
        "# Log\n\n## [Unreleased]\n\n## [1.4.0] - 2026-10-14\n\n### Added\n\n"
        "- **Flags** in `x`\n- [Site](https://e.x)\n\nEnd.\n\n## [1.3.0]\n\n- old\n"
    )
    body = changelog.section(text, "1.4.0")
    assert body is not None
    assert "old" not in body
    assert changelog.section(text, "9.9.9") is None
    assert changelog.section(text, "Unreleased") is None
    assert changelog.to_bbcode(body) == (
        "[b]Added[/b]\n\n[list]\n[*][b]Flags[/b] in x\n[*][url=https://e.x]Site[/url]\n[/list]\n\nEnd."
    )


def test_signature_verification(key: KeyPair) -> None:
    """Valid signatures verify; changed bytes, another key or garbage do not.

    Args:
        key: Session key.
    """
    public = public_key_from_xml(key.xml(private=True))
    signature = key.sign(b"manifest")
    assert verify_signature(public, b"manifest", signature)
    assert not verify_signature(public, b"manifest!", signature)
    assert not verify_signature(public, b"manifest", "not base64!")
    other = public_key_from_xml(KeyPair(public.modulus + 2, 3).xml(private=False))
    assert not verify_signature(other, b"manifest", signature)


def test_reviewed_hashes_read_the_real_deployment_table() -> None:
    """DEPLOYMENT.md pins exactly the three CAPIE binaries."""
    table = reviewed_hashes(
        (REPO / "plugin/capie/deploy/DEPLOYMENT.md").read_text(encoding="utf-8")
    )
    assert set(table) == {"capie_core.dll", "capie_editor.dll", "capie_injector.exe"}


def test_pinned_block_round_trip_on_the_real_installer_configuration() -> None:
    """The rendered block replaces the real one and reads back; the rest of the file stays."""
    path = REPO / "tools/bootstrap/src/Shared/Configuration.ps1"
    original = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    pinned = PinnedRelease(
        "1.4.0",
        "CAPIE-1.4.0-win64.zip",
        "B" * 64,
        (("RenameAnythingLauncher.exe", "C" * 64), ("mod/capie.mod", "D" * 64)),
        ("E" * 64,),
    )
    block = render_pinned_block(pinned)
    new = replace_pinned_block(original, block)
    assert pinned_fields(new) == {
        "Version": "1.4.0",
        "ZipName": "CAPIE-1.4.0-win64.zip",
        "ZipSha256": "B" * 64,
    }
    assert new.split("$script:RaPinnedRelease")[0] == original.split("$script:RaPinnedRelease")[0]
    assert block.isascii()


def test_reporter_redacts_and_colours(tmp_path: Path) -> None:
    """Secrets never reach the console or the log; NO_COLOR and TERM=dumb turn colour off.

    Args:
        tmp_path: pytest temporary folder.
    """
    stream = io.StringIO()
    log = tmp_path / "logs" / "run.log"
    reporter = Reporter(stream, color=True, log_path=log)
    reporter.redact("s3cr3t-key")
    reporter.record("x", Status.FAIL, "header x-apikey: s3cr3t-key")
    assert "s3cr3t-key" not in stream.getvalue()
    assert "s3cr3t-key" not in log.read_text(encoding="utf-8")
    assert "\x1b[31m" in stream.getvalue()

    class Tty(io.StringIO):
        """A stream that claims to be a terminal."""

        @override
        def isatty(self) -> bool:
            """Pretend to be a console.

            Returns:
                True.
            """
            return True

    assert use_color(Tty(), {}, disabled=False)
    assert not use_color(Tty(), {"NO_COLOR": "1"}, disabled=False)
    assert not use_color(Tty(), {"TERM": "dumb"}, disabled=False)
    assert not use_color(Tty(), {}, disabled=True)
    assert not use_color(io.StringIO(), {}, disabled=False)


def test_bbcode_joins_wrapped_list_items() -> None:
    """A bullet wrapped onto an indented line stays one list item."""
    text = "- First release: the switch and the\n  status line.\n- Second."
    assert (
        changelog.to_bbcode(text)
        == "[list]\n[*]First release: the switch and the status line.\n[*]Second.\n[/list]"
    )
