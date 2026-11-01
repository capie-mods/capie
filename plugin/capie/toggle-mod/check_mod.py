"""Structure checks of the "CAPIE" switch mod (plugin/capie/toggle-mod).

Offline and read-only. Checks the descriptors, that the launcher recognises the same
descriptor path, the thumbnail, that the mod ships nothing but its descriptor and thumbnail (no
gameplay folder, no interface or graphics file, so it never replaces a file that the game or a
total conversion such as Kaiserreich ships), and the player descriptions. Rename Anything's
test-session script comes from another repository; its copy in
``plugin/capie/tests/fixtures/external`` is read. The comparison with the game's own files uses
the HOI4 installation when it is present (read-only) and is reported as skipped otherwise.

Usage:
    python -I plugin/capie/toggle-mod/check_mod.py [--game "C:/.../Hearts of Iron IV"]
Exit code 0 when every check passes.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
"""Folder of this script (``plugin/capie/toggle-mod``)."""

REPO = HERE.parents[2]
"""Repository root."""

MOD = HERE / "capie"
"""Content folder of the switch mod (uploaded to the Workshop)."""

LOCAL_DESCRIPTOR = HERE / "capie.mod"
"""Local registration file that points the launcher at ``mod/capie``."""

RELEASE_TOML = REPO / "tools" / "release" / "release.toml"
"""Release configuration whose descriptor name must equal ``LOCAL_NAME``."""

EXTERNAL = REPO / "plugin" / "capie" / "tests" / "fixtures" / "external"
"""Vendored copies of files from other repositories (pinned in its ``lock.json``)."""

TEST_SESSION = EXTERNAL / "rename-anything" / "tools" / "test-session.ps1"
"""Rename Anything's test-session script; its CAPIE descriptor name differs from the Workshop's."""

WORKSHOP_NAME = "CAPIE"
"""Name of the Workshop item, as its ``descriptor.mod`` declares it."""

LOCAL_NAME = "CAPIE (local copy)"
"""Name of the local copy registration, which differs from the Workshop name."""

WORKSHOP_ID = "3819929471"
"""Steam Workshop ID of the switch mod, which the launcher must know."""

LAUNCHER_CONFIG = REPO / "plugin" / "capie" / "src" / "launcher" / "config.hpp"
"""Launcher source that compiles in the descriptor path it looks for."""

DEFAULT_GAME = Path("C:/Program Files (x86)/Steam/steamapps/common/Hearts of Iron IV")
"""Default HOI4 installation, read only for the vanilla comparison."""


THUMBNAIL_EDGE = 512
"""Edge of the square preview image in pixels."""

THUMBNAIL_MAX_BYTES = 1024 * 1024
"""Upper bound of the preview image size (Steam rejects 1 MB and more)."""

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
"""First eight bytes of every PNG file."""

PNG_SIZE_OFFSET = 16
"""Offset of the IHDR width and height in a PNG file."""

PNG_HEADER_BYTES = 24
"""Bytes needed to read the PNG signature, IHDR header, width and height."""

GAMEPLAY_FOLDERS = ("common", "events", "history", "map", "music", "sound", "portraits")
"""Folders that would carry gameplay content; the switch mod has none of them."""

INTERFACE_FOLDERS = ("interface", "gfx")
"""Folders of interface and graphics files; the switch mod has none of them."""

INTERFACE_SUFFIXES = (".gui", ".gfx")
"""Interface file types; any of them could replace a vanilla or total-conversion file."""

MOD_ENTRIES = {"descriptor.mod", "thumbnail.png"}
"""The only entries of the mod folder: the switch is the descriptor itself."""

WORKSHOP_DIR = REPO / "docs" / "assets" / "promo" / "workshop"
"""Single home of the Workshop descriptions (BBCode), one file per language."""

WORKSHOP_TEXTS = {"en": WORKSHOP_DIR / "capie.bbcode", "pl": WORKSHOP_DIR / "capie.pl.bbcode"}
"""Workshop descriptions of the switch mod; English is the release's description_file."""


class Checks:
    """Collects check results."""

    def __init__(self) -> None:
        """Starts with no results."""
        self.failures: list[str] = []
        self.passed = 0

    def check(self, label: str, *, ok: bool) -> None:
        """Records one check and prints failures.

        Args:
            label: What was checked.
            ok: Whether the check passed.
        """
        if ok:
            self.passed += 1
        else:
            self.failures.append(label)
            print(f"FAIL {label}")


def descriptor_fields(text: str) -> dict[str, str]:
    """Reads key="value" lines of a .mod descriptor.

    Args:
        text: Descriptor text.

    Returns:
        Field name to value (tags are not included).
    """
    return dict(re.findall(r'^([a-z_]+)="([^"]*)"\s*$', text, flags=re.MULTILINE))


def check_descriptors(checks: Checks) -> None:
    """Descriptor fields, the local registration file and the launcher identity.

    Args:
        checks: Result collector.
    """
    inner = descriptor_fields((MOD / "descriptor.mod").read_text(encoding="utf-8"))
    outer = descriptor_fields(LOCAL_DESCRIPTOR.read_text(encoding="utf-8"))
    for name, fields in (("descriptor.mod", inner), ("capie.mod", outer)):
        checks.check(
            f"{name}: supported_version 1.19.*", ok=fields.get("supported_version") == "1.19.*"
        )
        checks.check(
            f"{name}: picture is thumbnail.png", ok=fields.get("picture") == "thumbnail.png"
        )
        checks.check(
            f"{name}: version",
            ok=re.fullmatch(r"\d+\.\d+\.\d+", fields.get("version", "")) is not None,
        )
    checks.check("descriptor.mod has no path (the Workshop adds it)", ok="path" not in inner)
    checks.check("capie.mod points at mod/capie", ok=outer.get("path") == "mod/capie")
    checks.check(
        "descriptor.mod (Workshop item) is named CAPIE", ok=inner.get("name") == WORKSHOP_NAME
    )
    checks.check(
        "capie.mod (local copy) has its own name",
        ok=outer.get("name") == LOCAL_NAME,
    )
    checks.check(
        "local copy name differs from the Workshop name (HOI4 skips a Workshop twin)",
        ok=outer.get("name") != inner.get("name"),
    )
    release = RELEASE_TOML.read_text(encoding="utf-8")
    named = re.search(r'^descriptor_name\s*=\s*"([^"]+)"', release, flags=re.MULTILINE)
    checks.check(
        "release.toml names the bundled local copy like capie.mod",
        ok=named is not None and named.group(1) == LOCAL_NAME,
    )
    session = TEST_SESSION.read_text(encoding="utf-8")
    dev = re.search(r"\$script:TsCapieModName = '([^']+)'", session)
    checks.check(
        "test-session.ps1 names its CAPIE descriptor differently from the Workshop item",
        ok=dev is not None and dev.group(1) not in {WORKSHOP_NAME, LOCAL_NAME},
    )
    workshop = re.search(
        r'capie_mod_workshop_id\[\] = "(\d+)"', LAUNCHER_CONFIG.read_text(encoding="utf-8")
    )
    checks.check(
        "launcher knows the Workshop ID of the switch mod",
        ok=workshop is not None and workshop.group(1) == WORKSHOP_ID,
    )
    config = LAUNCHER_CONFIG.read_text(encoding="utf-8")
    match = re.search(r'capie_mod_descriptor\[\] = "([^"]+)"', config)
    checks.check(
        "launcher identity equals the local descriptor path (mod/capie.mod)",
        ok=match is not None and match.group(1) == "mod/" + LOCAL_DESCRIPTOR.name,
    )


def check_thumbnail(checks: Checks) -> None:
    """Thumbnail format, size and freshness.

    Args:
        checks: Result collector.
    """
    data = (MOD / "thumbnail.png").read_bytes()
    checks.check("thumbnail is a PNG", ok=data.startswith(PNG_SIGNATURE))
    if len(data) >= PNG_HEADER_BYTES:
        width, height = struct.unpack(">II", data[PNG_SIZE_OFFSET:PNG_HEADER_BYTES])
        checks.check(
            "thumbnail is 512 x 512", ok=width == THUMBNAIL_EDGE and height == THUMBNAIL_EDGE
        )
    checks.check("thumbnail is below 1 MiB (Workshop limit)", ok=len(data) < THUMBNAIL_MAX_BYTES)
    sys.path.insert(0, str(HERE))
    try:
        import make_thumbnail  # noqa: PLC0415 - local generator next to this file

        fresh = make_thumbnail.encode(make_thumbnail.draw(make_thumbnail.load_colors()))
        checks.check("thumbnail matches make_thumbnail.py and tokens.json", ok=fresh == data)
    finally:
        sys.path.pop(0)


def check_no_overrides(checks: Checks, game: Path) -> None:
    """The mod replaces no file of the game, above all no interface or graphics file.

    Total conversions ship their own ``interface/frontendmainview.gui`` and other interface
    files. A file at the same path in a mod that loads later replaces theirs whole, so the
    switch mod must not ship any.

    Args:
        checks: Result collector.
        game: HOI4 installation for the comparison with the game's files.
    """
    files = sorted(p.relative_to(MOD).as_posix() for p in MOD.rglob("*") if p.is_file())
    for folder in INTERFACE_FOLDERS:
        checks.check(
            f"no {folder}/ folder (the switch mod overrides no interface file)",
            ok=not (MOD / folder).exists(),
        )
    checks.check(
        "no .gui or .gfx file anywhere in the mod",
        ok=not [f for f in files if f.lower().endswith(INTERFACE_SUFFIXES)],
    )
    if (game / "interface").is_dir():
        shadowed = [f for f in files if (game / f).exists()]
        checks.check(
            f"no mod file has the path of a game file ({', '.join(shadowed) or 'none'})",
            ok=not shadowed,
        )
    else:
        print(f"SKIP comparison with the game's files: {game / 'interface'} not found")


def check_content(checks: Checks) -> None:
    """No gameplay content; player descriptions present and in the house voice.

    Args:
        checks: Result collector.
    """
    for folder in GAMEPLAY_FOLDERS:
        checks.check(
            f"no {folder}/ folder (switch mod carries no gameplay)", ok=not (MOD / folder).exists()
        )
    checks.check(
        "mod folder holds only descriptor.mod and thumbnail.png",
        ok={p.name for p in MOD.iterdir()} == MOD_ENTRIES,
    )
    for language, path in WORKSHOP_TEXTS.items():
        text = path.read_text(encoding="utf-8")
        checks.check(f"description {language}: title", ok="[h1]CAPIE[/h1]" in text)
        checks.check(f"description {language}: no exclamation marks", ok="!" not in text)
        checks.check(f"description {language}: explains the switch", ok="dlc_load.json" in text)


def main() -> int:
    """Runs every check.

    Returns:
        The process exit code.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME, help="HOI4 folder (read only)")
    args = parser.parse_args()
    checks = Checks()
    check_descriptors(checks)
    check_thumbnail(checks)
    check_no_overrides(checks, args.game)
    check_content(checks)
    print(f"switch mod checks: {checks.passed} passed, {len(checks.failures)} failed")
    return 0 if not checks.failures else 1


if __name__ == "__main__":
    sys.exit(main())
