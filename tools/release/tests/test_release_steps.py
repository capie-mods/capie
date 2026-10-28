"""Step tests of the release tool on a synthetic repository with fake gh, steamcmd and HTTP.

Every external effect is answered by the fakes in ``fakes.py``; the tests check what
the tool would have sent, and that nothing is sent without the required confirmation.
"""

from __future__ import annotations

import hashlib
import json
import zipfile
from typing import TYPE_CHECKING, override

from fakes import (
    BINARIES,
    COMMIT,
    GAME_SHA,
    IDENTITY_ASCII,
    FakePrompter,
    FakeRunner,
    Harness,
    KeyPair,
    bundle_inputs_json,
    make_repo,
)
from releasekit import workshop
from releasekit.package import (
    BundleEntry,
    contains_string,
    entry_problems,
    identity_problems,
    local_inner_descriptor,
    switch_mod_descriptor,
    write_zip,
)
from releasekit.vdf import parse_vdf, render_vdf, vdf_published_id
from releasekit.workshop import is_excluded, workshop_descriptor

if TYPE_CHECKING:
    from pathlib import Path

    import pytest

EXPECTED_ENTRIES = [
    "RenameAnythingLauncher.exe",
    "runtime/capie_core.dll",
    "runtime/capie_editor.dll",
    "runtime/capie_injector.exe",
    "mod/capie.mod",
    "mod/capie/descriptor.mod",
    "mod/capie/interface/capie.gui",
    "mod/capie/thumbnail.png",
]
"""ZIP entries of the synthetic bundle, in order."""

NEW_STEAM_ID = 3000000099
"""ID the fake SteamCMD assigns to a new item."""

EXISTING_STEAM_ID = 3000000001
"""ID of the already published switch mod in the synthetic configuration."""


def _build(harness: Harness, *extra: str) -> int:
    """Run the build step with the harness key.

    Args:
        harness: Test harness.
        *extra: More options.

    Returns:
        Exit code.
    """
    return harness.run("build", "--signing-key", str(harness.key_file), *extra)


# --- pure helpers --------------------------------------------------------------------


def test_zip_is_reproducible_and_installer_safe(tmp_path: Path) -> None:
    """Equal inputs give equal bytes; unsafe, duplicate and launcher-less bundles are refused.

    Args:
        tmp_path: pytest temporary folder.
    """
    entries = [BundleEntry("RenameAnythingLauncher.exe", b"a"), BundleEntry("mod/capie.mod", b"b")]
    write_zip(tmp_path / "one.zip", entries)
    write_zip(tmp_path / "two.zip", entries)
    assert (tmp_path / "one.zip").read_bytes() == (tmp_path / "two.zip").read_bytes()
    assert entry_problems(entries, "RenameAnythingLauncher.exe") == []
    bad = [
        BundleEntry("../x.dll", b""),
        BundleEntry("a b.txt", b""),
        BundleEntry("X", b""),
        BundleEntry("x", b""),
    ]
    problems = entry_problems(bad, "RenameAnythingLauncher.exe")
    assert len([p for p in problems if "unsafe" in p]) == 2  # noqa: PLR2004 - "../" and the space
    assert any("duplicate" in p for p in problems)
    assert any("launcher" in p for p in problems)


def test_descriptor_rewrites() -> None:
    """capie.mod gains path; the Workshop copy loses path and gains remote_file_id."""
    original = 'version="1.0.0"\r\nname="CAPIE"\r\npath="C:/x"\r\nremote_file_id="1"\r\n'
    assert (
        switch_mod_descriptor(original, "mod/capie", "CAPIE (local copy)").decode()
        == 'version="1.0.0"\nname="CAPIE (local copy)"\npath="mod/capie"\n'
    )
    assert (
        local_inner_descriptor(original, "CAPIE (local copy)").decode()
        == 'version="1.0.0"\nname="CAPIE (local copy)"\n'
    )
    assert workshop_descriptor(original, 0) == 'version="1.0.0"\nname="CAPIE"\n'
    assert (
        workshop_descriptor(original, 77) == 'version="1.0.0"\nname="CAPIE"\nremote_file_id="77"\n'
    )


def test_vdf_and_exclusions() -> None:
    """VDF values carry no quote or backslash; developer files are excluded by name or folder."""
    text = render_vdf([("contentfolder", "C:\\a\\b"), ("changenote", 'say "hi"')])
    assert '"contentfolder"\t"C:/a/b"' in text
    assert '"changenote"\t"say \'hi\'"' in text
    assert vdf_published_id(text) == 0
    assert vdf_published_id('"publishedfileid"\t"123"') == 123  # noqa: PLR2004
    assert is_excluded("NOTES.md", ("*.md",))
    assert is_excluded("gfx/__pycache__/x.pyc", ("__pycache__",))
    assert is_excluded(".git/config", (".*",))
    assert not is_excluded("common/rn.txt", ("*.md", ".*"))


# --- preflight -----------------------------------------------------------------------


def test_preflight_fails_on_a_dirty_tree_and_skips_commands_without_the_game(
    harness: Harness,
) -> None:
    """A dirty tree fails; commands needing the game are skipped, others run.

    Args:
        harness: Test harness.
    """
    harness.runner.status = " M tools/gen.py\n"
    assert harness.run("preflight") == 1
    assert "working tree not clean" in harness.output
    assert "SKIP  generator: needs game_dir" in harness.output
    assert any(call[1:3] == ("-m", "pytest") for call in harness.runner.calls)


def test_preflight_reports_stale_generator_output(harness: Harness, tmp_path: Path) -> None:
    """A generator that changes mod/ fails the clean_after check.

    Args:
        harness: Test harness.
        tmp_path: pytest temporary folder.
    """
    game = tmp_path / "game"
    game.mkdir()
    (game / "hoi4.exe").write_bytes(b"not the game")
    runner = harness.runner

    def dirty_after_gen(call: tuple[str, ...]) -> None:
        """Make mod/ dirty once gen.py ran.

        Args:
            call: Command line.
        """
        if "tools/gen.py" in call:
            runner.status = " M mod/common/rn.txt\n"

    runner.on_run = dirty_after_gen
    assert harness.run("preflight", "--game-dir", str(game)) == 1
    assert "generator changed 1 tracked file(s) in mod: output is stale" in harness.output
    assert "is not the supported build" in harness.output


# --- build ---------------------------------------------------------------------------


def test_build_writes_the_bundle_manifest_and_checksums(harness: Harness) -> None:
    """The ZIP has the installer layout, the manifest matches it and the signature verifies.

    Args:
        harness: Test harness.
    """
    assert _build(harness) == 0, harness.output
    zip_path = harness.out / "CAPIE-1.4.0-win64.zip"
    with zipfile.ZipFile(zip_path) as archive:
        assert archive.namelist() == EXPECTED_ENTRIES
        assert archive.read("runtime/capie_core.dll") == BINARIES["capie_core.dll"]
        assert archive.read("mod/capie.mod").decode().endswith('path="mod/capie"\n')
    manifest = json.loads((harness.out / "release-manifest.json").read_text(encoding="utf-8"))
    assert manifest["supportedGameSha256"] == [GAME_SHA]
    assert "signature verifies with the installer public key" in harness.output
    sums = (harness.out / "SHA256SUMS.txt").read_text(encoding="ascii")
    assert hashlib.sha256(zip_path.read_bytes()).hexdigest() in sums
    info = json.loads((harness.out / "build-info.json").read_text(encoding="utf-8"))
    assert info["commit"] == COMMIT
    assert "installer pins name 'RenameAnything-0.1.0-win64.zip'" in harness.output
    assert "TO-BE-PINNED" in (
        harness.root / "tools/bootstrap/src/Shared/Configuration.ps1"
    ).read_text(encoding="utf-8")


def test_build_write_pins_updates_the_installer(harness: Harness) -> None:
    """--write-pins puts the new ZIP hash into Configuration.ps1 and rebuilds install.ps1.

    Args:
        harness: Test harness.
    """
    assert _build(harness, "--write-pins") == 0, harness.output
    text = (harness.root / "tools/bootstrap/src/Shared/Configuration.ps1").read_text(
        encoding="utf-8"
    )
    zip_sha = (
        hashlib.sha256((harness.out / "CAPIE-1.4.0-win64.zip").read_bytes()).hexdigest().upper()
    )
    assert f"ZipSha256           = '{zip_sha}'" in text
    assert "'mod/capie/thumbnail.png'" in text
    assert any("build-install.ps1" in " ".join(c) for c in harness.runner.calls)


def test_build_refuses_a_key_inside_the_repository(harness: Harness) -> None:
    """The private key must live outside the repository.

    Args:
        harness: Test harness.
    """
    inside = harness.root / "release.private.xml"
    inside.write_text(harness.key_file.read_text(encoding="utf-8"), encoding="utf-8")
    assert harness.run("build", "--signing-key", str(inside)) == 1
    assert "inside the repository" in harness.output
    assert not harness.out.exists()


def test_build_refuses_unreviewed_binaries(harness: Harness) -> None:
    """A CAPIE DLL that differs from bundle-inputs.json stops the build before packaging.

    Args:
        harness: Test harness.
    """
    (harness.root / "plugin/build/capie/capie_core.dll").write_bytes(b"MZ rebuilt")
    assert _build(harness) == 1
    assert "!= reviewed" in harness.output
    assert not (harness.out / "CAPIE-1.4.0-win64.zip").exists()


def _replace_binary(harness: Harness, name: str, data: bytes) -> None:
    """Swap one reviewed CAPIE binary everywhere and re-pin it, so only its contents differ.

    Args:
        harness: Test harness.
        name: File name (``capie_core.dll`` ...).
        data: New contents.
    """
    binaries = dict(BINARIES)
    binaries[name] = data
    for folder in ("plugin/build/capie", "plugin/build/capie-launcher/runtime"):
        (harness.root / folder / name).write_bytes(data)
    (harness.root / "plugin/capie/deploy/bundle-inputs.json").write_text(
        bundle_inputs_json(binaries), encoding="utf-8"
    )


def test_build_refuses_a_binary_built_for_another_game(harness: Harness) -> None:
    """A reviewed DLL without the header's game_exe_sha256 stops the build before packaging.

    Args:
        harness: Test harness.
    """
    other = ("a" * 64).encode("ascii")
    _replace_binary(harness, "capie_editor.dll", b"MZ editor\0" + other + b"\x001.4.0\0")
    assert _build(harness) == 1
    assert "runtime/capie_editor.dll does not contain game_exe_sha256" in harness.output
    assert "do not match game_build.hpp" in harness.output
    assert not (harness.out / "CAPIE-1.4.0-win64.zip").exists()


def test_build_refuses_a_stale_release_string(harness: Harness) -> None:
    """A binary that still says the previous capie_release is not packaged.

    Args:
        harness: Test harness.
    """
    stale = b"MZ core\0" + GAME_SHA.lower().encode("ascii") + b"\x001.3.9\0"
    _replace_binary(harness, "capie_core.dll", stale)
    assert _build(harness) == 1
    assert "runtime/capie_core.dll does not contain capie_release '1.4.0'" in harness.output


def test_build_checks_the_launcher_and_the_proxy(harness: Harness) -> None:
    """The launcher and the optional proxy are PE files too and must carry both strings.

    Args:
        harness: Test harness.
    """
    proxy = harness.root / "plugin/build/capie-proxy/version.dll"
    proxy.parent.mkdir(parents=True)
    proxy.write_bytes(b"MZ proxy without identity")
    (harness.root / "plugin/build/capie-launcher/RenameAnythingLauncher.exe").write_bytes(
        b"MZ old launcher"
    )
    assert _build(harness) == 1
    assert "proxy/version.dll does not contain game_exe_sha256" in harness.output
    assert "RenameAnythingLauncher.exe does not contain capie_release" in harness.output
    proxy.write_bytes(b"MZ proxy" + IDENTITY_ASCII)
    (harness.root / "plugin/build/capie-launcher/RenameAnythingLauncher.exe").write_bytes(
        b"MZ launcher" + IDENTITY_ASCII
    )
    assert _build(harness) == 0, harness.output
    assert "5 PE files carry game_exe_sha256" in harness.output


def test_build_refuses_reviewed_files_that_disagree_with_bundle_inputs(harness: Harness) -> None:
    """release.toml's reviewed_files and bundle-inputs.json must name the same binaries.

    Args:
        harness: Test harness.
    """
    binaries = {k: v for k, v in BINARIES.items() if k != "capie_injector.exe"}
    (harness.root / "plugin/capie/deploy/bundle-inputs.json").write_text(
        bundle_inputs_json(binaries), encoding="utf-8"
    )
    assert _build(harness) == 1
    assert "reviewed_files is" in harness.output


def test_contains_string_finds_ascii_and_utf16_tokens_only() -> None:
    """Both encodings count; a longer version or hash that only starts the same does not."""
    sha = GAME_SHA.lower()
    assert contains_string(b"x\0" + sha.encode() + b"\0", GAME_SHA, ignore_case=True)
    assert contains_string(b"MZ" + sha.upper().encode("utf-16-le"), sha, ignore_case=True)
    assert not contains_string(b"MZ" + sha.encode(), GAME_SHA)
    assert contains_string(b"v1.5.1\0", "1.5.1")
    assert contains_string(b"CAPIE 1.5.1 loaded", "1.5.1")
    assert not contains_string(b"1.5.10\0", "1.5.1")
    assert not contains_string(b"11.5.1\0", "1.5.1")
    assert not contains_string(b"1.5.1.2\0", "1.5.1")
    assert not contains_string(b"\0" + sha.encode() + b"f", sha)
    assert contains_string("CAPIE 1.5.1\0".encode("utf-16-le"), "1.5.1")
    assert not contains_string("1.5.10\0".encode("utf-16-le"), "1.5.1")


def test_identity_problems_ignore_text_files() -> None:
    """Only .dll/.exe (or MZ-headed) entries are checked; a bundle without one is refused."""
    text = BundleEntry("mod/capie.mod", b'name="CAPIE"\n')
    assert identity_problems([text], GAME_SHA, "1.4.0") == [
        "the bundle has no .dll/.exe file to check"
    ]
    good = BundleEntry("runtime/capie_core.dll", BINARIES["capie_core.dll"])
    assert identity_problems([text, good], GAME_SHA, "1.4.0") == []
    renamed = BundleEntry("runtime/capie_core.bin", b"MZ")
    assert len(identity_problems([renamed], GAME_SHA, "1.4.0")) == 2  # noqa: PLR2004 - both strings


def test_rebuild_capie_stops_for_re_pinning(harness: Harness, tmp_path: Path) -> None:
    """A fresh build.ps1 run with new hashes stops and names the files to re-pin.

    The build goes to the scratch folder; the reviewed folder keeps its files.

    Args:
        harness: Test harness.
        tmp_path: pytest temporary folder.
    """
    runner = harness.runner
    scratch = harness.root / "plugin/build/capie-rebuild"

    def rebuild(call: tuple[str, ...]) -> None:
        """Simulate build.ps1 producing new binaries in the folder it was given.

        Args:
            call: Command line.
        """
        if "plugin/capie/build.ps1" in call:
            assert call[call.index("-OutputDirectory") + 1] == "plugin/build/capie-rebuild"
            scratch.mkdir(parents=True)
            for name, data in BINARIES.items():
                (scratch / name).write_bytes(data)
            (scratch / "capie_editor.dll").write_bytes(b"MZ new editor")

    runner.on_run = rebuild
    code = _build(harness, "--rebuild-capie", "--minhook-dir", str(tmp_path))
    assert code == 1
    assert "re-pin them in plugin/capie/deploy/bundle-inputs.json" in harness.output
    changed = harness.output.split("and run 'build' again: ")[-1]
    assert "capie_editor.dll" in changed
    assert "capie_core.dll" not in changed
    for name, data in BINARIES.items():
        assert (harness.root / "plugin/build/capie" / name).read_bytes() == data


def test_rebuild_capie_refuses_the_reviewed_folder(harness: Harness, tmp_path: Path) -> None:
    """A rebuild_output that is the reviewed folder (from bundle-inputs.json) never builds.

    Args:
        harness: Test harness.
        tmp_path: pytest temporary folder.
    """
    text = harness.config.read_text(encoding="utf-8")
    harness.config.write_text(
        text.replace("capie-rebuild", "capie"),
        encoding="utf-8",
    )
    assert _build(harness, "--rebuild-capie", "--minhook-dir", str(tmp_path)) == 1
    assert "is the reviewed build folder" in harness.output
    assert not any("plugin/capie/build.ps1" in c for c in harness.runner.calls)


def test_reviewed_binaries_are_read_from_the_bundle_inputs_sources(harness: Harness) -> None:
    """The reviewed folder is the one bundle-inputs.json names; release.toml does not name it.

    Args:
        harness: Test harness.
    """
    (harness.root / "plugin/build/capie").rename(harness.root / "plugin/build/capie-reviewed")
    inputs = harness.root / "plugin/capie/deploy/bundle-inputs.json"
    text = inputs.read_text(encoding="utf-8")
    inputs.write_text(text.replace("build/capie/", "build/capie-reviewed/"), encoding="utf-8")
    assert _build(harness) == 0, harness.output
    inputs.write_text(
        text.replace("capie/capie_core.dll", "other/capie_core.dll"), encoding="utf-8"
    )
    assert _build(harness) == 1
    assert "sources must share one folder" in harness.output


def test_build_dry_run_writes_nothing(harness: Harness) -> None:
    """A dry run lists the ZIP entries and commands and creates no file.

    Args:
        harness: Test harness.
    """
    assert harness.run("build", "--dry-run") == 0, harness.output
    assert "PLAN    mod/capie.mod" in harness.output
    assert not harness.out.exists()
    assert not any("-MakeManifest" in c for c in harness.runner.calls)


def test_build_without_installer_key_warns(tmp_path: Path, key: KeyPair) -> None:
    """With the placeholder public key the signature is checked against the key itself.

    Args:
        tmp_path: pytest temporary folder.
        key: Session key.
    """
    root = tmp_path / "repo"
    config = make_repo(root, key, installer_key=False)
    key_file = tmp_path / "k.xml"
    key_file.write_text(key.xml(private=True), encoding="utf-8")
    harness = Harness(root=root, config=config, key_file=key_file, runner=FakeRunner(root, key))
    assert _build(harness) == 0, harness.output
    assert "installer public key is a placeholder" in harness.output
    assert "public part of the signing key" in harness.output


# --- GitHub release ------------------------------------------------------------------


def test_release_is_a_draft_prerelease_by_default(harness: Harness) -> None:
    """The ``gh release create`` call gets --draft and --prerelease, the exact commit and the notes.

    Args:
        harness: Test harness.
    """
    assert _build(harness) == 0
    assert harness.run("release") == 0, harness.output
    (create,) = harness.runner.mutating()
    assert create[:4] == ("gh", "release", "create", "v1.4.0")
    assert "--draft" in create
    assert "--prerelease" in create
    assert create[create.index("--target") + 1] == COMMIT
    assert create[create.index("--repo") + 1] == "capie-mods/capie"
    notes = (harness.out / "release-notes.md").read_text(encoding="utf-8")
    assert notes.startswith("- Flags.")
    assert "Not declared multiplayer-compatible" in notes
    assert "Pending" in notes


def test_publish_needs_a_typed_confirmation(harness: Harness) -> None:
    """--publish without the typed word creates nothing.

    Args:
        harness: Test harness.
    """
    assert _build(harness, "--write-pins") == 0
    assert harness.run("release", "--publish") == 1
    assert harness.runner.mutating() == []
    harness.prompter.words["publish"] = True
    assert harness.run("release", "--publish") == 0, harness.output
    (create,) = harness.runner.mutating()
    assert "--draft" not in create
    assert "--prerelease" in create


def test_release_refuses_an_existing_tag_or_unpushed_commit(harness: Harness) -> None:
    """An existing release or a commit missing on GitHub stops the step.

    Args:
        harness: Test harness.
    """
    assert _build(harness) == 0
    harness.runner.release_exists = True
    assert harness.run("release") == 1
    assert "already exists" in harness.output
    assert harness.runner.mutating() == []
    harness.runner.release_exists = False
    harness.runner.commit_on_remote = False
    assert harness.run("release") == 1
    assert "push it first" in harness.output
    assert harness.runner.mutating() == []


def _release_refused(harness: Harness, reason: str) -> None:
    """The release step stops with this reason and sends nothing to GitHub.

    Args:
        harness: Test harness.
        reason: Text expected in the output.
    """
    assert harness.run("release") == 1
    assert reason in harness.output
    assert "run 'build' again" in harness.output
    assert harness.runner.mutating() == []


def test_release_re_verifies_the_files_before_uploading(harness: Harness) -> None:
    """Unchanged build output passes the re-check.

    Args:
        harness: Test harness.
    """
    assert _build(harness) == 0
    assert harness.run("release") == 0, harness.output
    assert "ZIP, manifest, checksums and signature re-verified" in harness.output


def test_release_refuses_a_zip_the_manifest_does_not_describe(harness: Harness) -> None:
    """A ZIP rewritten after signing (other bytes, same name) is never uploaded.

    Args:
        harness: Test harness.
    """
    assert _build(harness) == 0
    zip_path = harness.out / "CAPIE-1.4.0-win64.zip"
    with zipfile.ZipFile(zip_path, "a") as archive:
        archive.writestr("extra.txt", "not signed")
    _release_refused(harness, "!= release-manifest.json")
    assert "the files inside CAPIE-1.4.0-win64.zip differ" in harness.output


def test_release_refuses_a_signature_that_does_not_verify(harness: Harness, key: KeyPair) -> None:
    """A signature over other bytes is never uploaded, even with matching checksums.

    Args:
        harness: Test harness.
        key: Session key.
    """
    assert _build(harness) == 0
    signature = harness.out / "release-manifest.json.sig"
    signature.write_text(key.sign(b"another manifest"), encoding="utf-8")
    sums = harness.out / "SHA256SUMS.txt"
    digest = hashlib.sha256(signature.read_bytes()).hexdigest()
    lines = [
        f"{digest}  {signature.name}" if line.endswith(signature.name) else line
        for line in sums.read_text(encoding="ascii").splitlines()
    ]
    sums.write_text("\n".join(lines) + "\n", encoding="ascii")
    _release_refused(harness, "does not verify with the installer public key")


def test_release_refuses_stale_checksums(harness: Harness) -> None:
    """SHA256SUMS.txt must list exactly the current ZIP, manifest and signature.

    Args:
        harness: Test harness.
    """
    assert _build(harness) == 0
    sums = harness.out / "SHA256SUMS.txt"
    sums.write_text("0" * 64 + "  CAPIE-1.4.0-win64.zip\n", encoding="ascii")
    _release_refused(harness, "SHA256SUMS.txt does not list exactly the current")


def test_release_refuses_without_the_installer_public_key(tmp_path: Path, key: KeyPair) -> None:
    """With the placeholder public key nothing can be verified, so nothing is uploaded.

    Args:
        tmp_path: pytest temporary folder.
        key: Session key.
    """
    root = tmp_path / "repo"
    config = make_repo(root, key, installer_key=False)
    key_file = tmp_path / "k.xml"
    key_file.write_text(key.xml(private=True), encoding="utf-8")
    harness = Harness(root=root, config=config, key_file=key_file, runner=FakeRunner(root, key))
    assert _build(harness) == 0, harness.output
    _release_refused(harness, "the signature cannot be verified")


def test_failed_rebuild_leaves_no_stale_manifest(harness: Harness) -> None:
    """A re-run that fails after writing the ZIP leaves no old manifest, signature or sums.

    Args:
        harness: Test harness.
    """
    assert _build(harness) == 0
    harness.runner.failing = ("-MakeManifest",)
    assert _build(harness) == 1
    for name in ("release-manifest.json", "release-manifest.json.sig", "SHA256SUMS.txt"):
        assert not (harness.out / name).exists()
    harness.runner.failing = ()
    assert harness.run("release") == 1
    assert "asset missing" in harness.output
    assert harness.runner.mutating() == []


# --- VirusTotal ----------------------------------------------------------------------


def test_virustotal_uploads_only_after_confirmation(harness: Harness) -> None:
    """Known files are only looked up; unknown ones wait for the typed 'upload'.

    Args:
        harness: Test harness.
    """
    assert _build(harness) == 0
    harness.env["VT_API_KEY"] = "vt-secret-123"
    known = hashlib.sha256(BINARIES["capie_core.dll"]).hexdigest()
    harness.transport.known[known] = {"malicious": 0, "undetected": 72}
    assert harness.run("virustotal") == 0, harness.output
    assert [r for r in harness.transport.requests if r[0] == "POST"] == []
    assert "upload not confirmed" in harness.output
    harness.prompter.words["upload"] = True
    assert harness.run("virustotal") == 0, harness.output
    posts = [r for r in harness.transport.requests if r[0] == "POST"]
    assert len(posts) == 4  # noqa: PLR2004 - ZIP, launcher, editor, injector (core was known)
    assert all(r[2]["x-apikey"] == "vt-secret-123" for r in harness.transport.requests)
    notes = (harness.out / "virustotal.md").read_text(encoding="utf-8")
    assert f"https://www.virustotal.com/gui/file/{known}" in notes
    assert "vt-secret-123" not in harness.output
    log = next((harness.root / "dist/release/logs").glob("*virustotal.log"))
    assert "vt-secret-123" not in log.read_text(encoding="utf-8")


def test_virustotal_without_key_fails_without_requests(harness: Harness) -> None:
    """No key in the environment: nothing is sent.

    Args:
        harness: Test harness.
    """
    assert _build(harness) == 0
    assert harness.run("virustotal") == 1
    assert "VT_API_KEY" in harness.output
    assert harness.transport.requests == []


# --- Workshop ------------------------------------------------------------------------


def test_new_workshop_item_is_hidden_and_its_id_is_stored(harness: Harness) -> None:
    """A new item is uploaded hidden with title; only the account name reaches SteamCMD.

    Args:
        harness: Test harness.
    """
    assert (
        harness.run("workshop", "--item", "rename-anything", "--steam-user", "maintainer", "--yes")
        == 0
    ), harness.output
    (call,) = harness.runner.interactive
    vdf_path = call[call.index("+workshop_build_item") + 1]
    assert call == ("steamcmd", "+login", "maintainer", "+workshop_build_item", vdf_path, "+quit")
    vdf = (harness.out / "workshop/rename-anything-0.1.0/item.vdf").read_text(encoding="utf-8")
    assert '"visibility"\t"2"' in vdf
    assert '"title"\t"Rename Anything"' in vdf
    assert '"appid"\t"394360"' in vdf
    assert "[list]\n[*]Renaming.\n[/list]" in vdf
    content = harness.out / "workshop/rename-anything-0.1.0/content"
    assert sorted(p.relative_to(content).as_posix() for p in content.rglob("*") if p.is_file()) == [
        "common/rn.txt",
        "descriptor.mod",
        "thumbnail.png",
    ]
    assert "path=" not in (content / "descriptor.mod").read_text(encoding="utf-8")
    config = harness.config.read_text(encoding="utf-8")
    assert f"published_file_id = {NEW_STEAM_ID}" in config
    assert f"published_file_id = {EXISTING_STEAM_ID}" in config


def test_existing_item_keeps_visibility_and_description(harness: Harness) -> None:
    """An update sends no visibility, title or description, and sets remote_file_id.

    Args:
        harness: Test harness.
    """
    assert (
        harness.run("workshop", "--item", "capie-switch", "--steam-user", "maintainer", "--yes")
        == 0
    ), harness.output
    vdf = (harness.out / "workshop/capie-switch-1.0.0/item.vdf").read_text(encoding="utf-8")
    assert '"visibility"' not in vdf
    assert '"description"' not in vdf
    assert f'"publishedfileid"\t"{EXISTING_STEAM_ID}"' in vdf
    descriptor = (harness.out / "workshop/capie-switch-1.0.0/content/descriptor.mod").read_text(
        encoding="utf-8"
    )
    assert f'remote_file_id="{EXISTING_STEAM_ID}"' in descriptor


def test_public_visibility_needs_a_typed_confirmation(harness: Harness) -> None:
    """--visibility public without the typed word never starts SteamCMD.

    Args:
        harness: Test harness.
    """
    args = (
        "workshop",
        "--item",
        "capie-switch",
        "--steam-user",
        "maintainer",
        "--yes",
        "--visibility",
        "public",
    )
    assert harness.run(*args) == 1
    assert harness.runner.interactive == []
    harness.prompter.words["public"] = True
    assert harness.run(*args) == 0, harness.output
    vdf = (harness.out / "workshop/capie-switch-1.0.0/item.vdf").read_text(encoding="utf-8")
    assert '"visibility"\t"0"' in vdf


def test_workshop_rejects_anything_but_an_account_name(harness: Harness) -> None:
    """A value with spaces (for example "user password") is refused before SteamCMD runs.

    Args:
        harness: Test harness.
    """
    assert (
        harness.run("workshop", "--item", "capie-switch", "--steam-user", "user hunter2", "--yes")
        == 1
    )
    assert harness.runner.interactive == []


def test_workshop_upload_needs_confirmation_without_yes(harness: Harness) -> None:
    """Without --yes the plain upload question must be answered with 'upload'.

    Args:
        harness: Test harness.
    """
    assert harness.run("workshop", "--item", "capie-switch", "--steam-user", "maintainer") == 0
    assert "Workshop upload not confirmed" in harness.output
    assert harness.runner.interactive == []


# --- whole run -----------------------------------------------------------------------


def test_dry_run_of_everything_sends_nothing(harness: Harness) -> None:
    """``all --dry-run`` shows every step and sends, uploads and writes nothing.

    Args:
        harness: Test harness.
    """
    harness.env["VT_API_KEY"] = "vt-secret-123"
    code = harness.run("all", "--dry-run", "--virustotal", "--steam-user", "maintainer")
    assert code == 0, harness.output
    for heading in ("Preflight", "Build", "VirusTotal", "GitHub release", "Steam Workshop"):
        assert f"== {heading}" in harness.output
    assert harness.runner.mutating() == []
    assert harness.runner.interactive == []
    assert harness.transport.requests == []
    assert not harness.out.exists()
    assert "steamcmd +login maintainer +workshop_build_item" in harness.output
    assert "\x1b[" not in harness.output


def test_dry_run_continues_after_a_failed_step(harness: Harness) -> None:
    """A dry run with a dirty tree still shows every later step; a real run stops.

    Args:
        harness: Test harness.
    """
    harness.runner.status = " M tools/gen.py\n"
    assert harness.run("all", "--dry-run", "--steam-user", "maintainer") == 1
    assert "== Steam Workshop" in harness.output
    assert harness.run("all", "--steam-user", "maintainer") == 1
    assert "== Build" not in harness.output
    assert "stopped after the preflight step" in harness.output


# --- Workshop: SteamCMD parser, one login, questions first, DRAFT -------------------


def test_one_steamcmd_run_logs_in_once_for_all_items(harness: Harness) -> None:
    """Two items: one SteamCMD call, one +login, two +workshop_build_item, in config order.

    Args:
        harness: Test harness.
    """
    assert harness.run("workshop", "--steam-user", "maintainer", "--yes") == 0, harness.output
    (call,) = harness.runner.interactive
    assert call.count("+login") == 1
    builds = [call[i + 1] for i, arg in enumerate(call) if arg == "+workshop_build_item"]
    assert [b.replace("\\", "/").split("/")[-2] for b in builds] == [
        "rename-anything-0.1.0",
        "capie-switch-1.0.0",
    ]
    assert call[-1] == "+quit"


def test_nothing_starts_before_every_answer(harness: Harness) -> None:
    """Visibility and upload questions come before SteamCMD; a declined one starts nothing.

    Args:
        harness: Test harness.
    """
    runner = harness.runner
    seen_interactive: list[int] = []

    class Recorder(FakePrompter):
        """Notes how many SteamCMD runs had happened at each question."""

        @override
        def confirm(self, question: str, word: str) -> bool:
            """Record, then answer from the table.

            Args:
                question: Question.
                word: Expected word.

            Returns:
                The scripted answer.
            """
            seen_interactive.append(len(runner.interactive))
            return super().confirm(question, word)

    harness.prompter = Recorder(words={"public": True, "upload": False})
    args = ("workshop", "--steam-user", "maintainer", "--visibility", "public")
    assert harness.run(*args) == 0
    assert seen_interactive == [0, 0, 0]
    assert runner.interactive == []
    assert not (harness.out / "workshop").exists()


def test_running_steamcmd_blocks_the_upload(harness: Harness) -> None:
    """A SteamCMD left over from an interrupted run stops the step before any question.

    Args:
        harness: Test harness.
    """
    harness.runner.steamcmd_running = True
    assert harness.run("workshop", "--steam-user", "maintainer") == 1
    assert "steamcmd.exe is still running" in harness.output
    assert harness.prompter.asked == []
    assert harness.runner.interactive == []


def test_draft_notes_are_never_sent(harness: Harness) -> None:
    """A change note or release section marked DRAFT fails preflight, workshop and release.

    Args:
        harness: Test harness.
    """
    notes = harness.root / "CHANGELOG.md"
    notes.write_text("## [0.1.0]\n\nDRAFT: not reviewed.\n\n- Renaming.\n", encoding="utf-8")
    assert harness.run("workshop", "--item", "rename-anything", "--steam-user", "m", "--yes") == 1
    assert "is still marked DRAFT" in harness.output
    assert harness.runner.interactive == []
    assert harness.run("preflight") == 1
    assert "CHANGELOG.md is still marked DRAFT" in harness.output
    capie = harness.root / "plugin/capie/CHANGELOG.md"
    capie.write_text("## [1.4.0]\n\nDRAFT\n\n- Flags.\n", encoding="utf-8")
    assert _build(harness) == 0
    assert harness.run("release") == 1
    assert harness.runner.mutating() == []


def test_update_description_of_an_existing_item(harness: Harness) -> None:
    """--update-description sends title and description but leaves visibility alone.

    Args:
        harness: Test harness.
    """
    args = ("workshop", "--item", "capie-switch", "--steam-user", "maintainer", "--yes")
    assert harness.run(*args, "--update-description") == 0, harness.output
    vdf = (harness.out / "workshop/capie-switch-1.0.0/item.vdf").read_text(encoding="utf-8")
    values = parse_vdf(vdf)
    assert values["description"] == "[h1]CAPIE[/h1] shows 'CAPIE 1.4'."
    assert values["title"] == "CAPIE"
    assert "visibility" not in values


def test_failed_steamcmd_still_keeps_created_ids(harness: Harness) -> None:
    """Exit code 9 fails the step, but an ID SteamCMD wrote is still stored.

    Args:
        harness: Test harness.
    """
    harness.runner.steamcmd_exit = 9
    assert harness.run("workshop", "--steam-user", "maintainer", "--yes") == 1
    assert "steamcmd exited with 9" in harness.output
    assert f"published_file_id = {NEW_STEAM_ID}" in harness.config.read_text(encoding="utf-8")


def test_a_vdf_steamcmd_would_misread_is_never_sent(
    harness: Harness, monkeypatch: pytest.MonkeyPatch
) -> None:
    """With the old escaping renderer the parse check stops the step before SteamCMD.

    Args:
        harness: Test harness.
        monkeypatch: pytest monkeypatch fixture.
    """

    def escaped(fields: list[tuple[str, str]]) -> str:
        """The renderer of the first real upload (backslash escapes).

        Args:
            fields: Keys and values.

        Returns:
            VDF text.
        """
        body = "".join(f'\t"{k}"\t"{v.replace(chr(34), chr(92) + chr(34))}"\n' for k, v in fields)
        return '"workshopitem"\n{\n' + body + "}\n"

    monkeypatch.setattr(workshop, "render_vdf", escaped)
    args = ("workshop", "--item", "capie-switch", "--steam-user", "maintainer", "--yes")
    assert harness.run(*args, "--update-description") == 1
    assert "item.vdf unexpected key" in harness.output
    assert harness.runner.interactive == []
