"""Structure checks of the "CAPIE" switch mod (plugin/capie/toggle-mod).

Offline and read-only. Checks the descriptors, that the launcher recognises the same
descriptor path, the thumbnail, the main-menu indicator, its localisation fallback (same key
and text as the CAPIE script SDK), that no gameplay folder and no vanilla interface file is
shipped, and the player descriptions. The vanilla comparison uses the HOI4 installation when
it is present (read-only) and is reported as skipped otherwise.

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
REPO = HERE.parents[2]
MOD = HERE / "capie"
LOCAL_DESCRIPTOR = HERE / "capie.mod"
LAUNCHER_CONFIG = REPO / "plugin" / "capie" / "src" / "launcher" / "config.hpp"
SDK_LOCALISATION = REPO / "sdk" / "capie-script-sdk" / "mod" / "localisation"
DEFAULT_GAME = Path("C:/Program Files (x86)/Steam/steamapps/common/Hearts of Iron IV")

THUMBNAIL_EDGE = 512
THUMBNAIL_MAX_BYTES = 1024 * 1024
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_SIZE_OFFSET = 16
PNG_HEADER_BYTES = 24
UTF8_BOM = b"\xef\xbb\xbf"
GAMEPLAY_FOLDERS = ("common", "events", "history", "map", "gfx", "music", "sound", "portraits")
MENU_WINDOW = "frontend_alpha_stamp"
STATUS_KEY = "CAPIE_STATUS"
POLISH_GLYPH_FONTS = ("hoi_18", "hoi_18mbs", "hoi_16mbs", "Arial12")
LANGUAGES = ("english", "polish")


class Checks:
    """Collects check results."""

    def __init__(self) -> None:
        """Starts with no results."""
        self.failures: list[str] = []
        self.passed = 0

    def check(self, label: str, *, ok: bool) -> None:
        """Records one check and prints failures."""
        if ok:
            self.passed += 1
        else:
            self.failures.append(label)
            print(f"FAIL {label}")


def descriptor_fields(text: str) -> dict[str, str]:
    """Reads key="value" lines of a .mod descriptor.

    Returns:
        Field name to value (tags are not included).
    """
    return dict(re.findall(r'^([a-z_]+)="([^"]*)"\s*$', text, flags=re.MULTILINE))


def balanced(text: str) -> bool:
    """Checks that braces are balanced outside comments and strings.

    Returns:
        True when every opened brace is closed in order.
    """
    depth = 0
    for line in text.splitlines():
        code = re.sub(r'"[^"]*"', '""', line.split("#", 1)[0])
        for ch in code:
            depth += {"{": 1, "}": -1}.get(ch, 0)
            if depth < 0:
                return False
    return depth == 0


def localisation_value(path: Path, key: str) -> str | None:
    """Finds the text of one key in a localisation file.

    Returns:
        The quoted text, or None when the key is missing.
    """
    text = path.read_bytes().removeprefix(UTF8_BOM).decode("utf-8")
    match = re.search(rf'^\s*{re.escape(key)}:\d*\s+"(.*)"\s*$', text, flags=re.MULTILINE)
    return str(match.group(1)) if match else None


def check_descriptors(checks: Checks) -> None:
    """Descriptor fields, the local registration file and the launcher identity."""
    inner = descriptor_fields((MOD / "descriptor.mod").read_text(encoding="utf-8"))
    outer = descriptor_fields(LOCAL_DESCRIPTOR.read_text(encoding="utf-8"))
    for name, fields in (("descriptor.mod", inner), ("capie.mod", outer)):
        checks.check(f"{name}: name is CAPIE", ok=fields.get("name") == "CAPIE")
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
    config = LAUNCHER_CONFIG.read_text(encoding="utf-8")
    match = re.search(r'capie_mod_descriptor\[\] = "([^"]+)"', config)
    checks.check(
        "launcher identity equals the local descriptor path (mod/capie.mod)",
        ok=match is not None and match.group(1) == "mod/" + LOCAL_DESCRIPTOR.name,
    )


def check_thumbnail(checks: Checks) -> None:
    """Thumbnail format, size and freshness."""
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


def check_indicator(checks: Checks, game: Path) -> None:
    """Main-menu indicator file and its localisation fallback."""
    gui_files = sorted((MOD / "interface").glob("*.gui"))
    checks.check(
        "one indicator .gui file", ok=[f.name for f in gui_files] == ["capie_menu_indicator.gui"]
    )
    gui = (MOD / "interface" / "capie_menu_indicator.gui").read_text(encoding="utf-8")
    checks.check("indicator .gui braces are balanced", ok=balanced(gui))
    checks.check(
        f"indicator defines the engine window {MENU_WINDOW}", ok=f'name = "{MENU_WINDOW}"' in gui
    )
    checks.check(f"indicator shows {STATUS_KEY}", ok=f'text = "{STATUS_KEY}"' in gui)
    font = re.search(r'font = "([^"]+)"', gui)
    checks.check(
        "indicator font has Polish glyphs",
        ok=font is not None and font.group(1) in POLISH_GLYPH_FONTS,
    )
    checks.check(
        "indicator has named elements",
        ok=re.search(r"^\s*name\s*=", gui, flags=re.MULTILINE) is not None,
    )
    for language in LANGUAGES:
        ours = MOD / "localisation" / language / f"capie_switch_l_{language}.yml"
        sdk = SDK_LOCALISATION / language / f"capie_sdk_v1_l_{language}.yml"
        raw = ours.read_bytes()
        checks.check(
            f"{language}: localisation has the UTF-8 BOM HOI4 needs", ok=raw.startswith(UTF8_BOM)
        )
        header = raw.removeprefix(UTF8_BOM).decode("utf-8").splitlines()[0]
        checks.check(f"{language}: header l_{language}:", ok=header == f"l_{language}:")
        value = localisation_value(ours, STATUS_KEY)
        checks.check(f"{language}: {STATUS_KEY} fallback present", ok=value is not None)
        checks.check(
            f"{language}: fallback equals the SDK text",
            ok=value == localisation_value(sdk, STATUS_KEY),
        )
    vanilla = game / "interface"
    if vanilla.is_dir():
        clashes = [f.name for f in gui_files if (vanilla / f.name).exists()]
        checks.check(f"no vanilla interface file is overridden ({clashes})", ok=not clashes)
        main_menu = (vanilla / "frontendmainview.gui").read_text(encoding="utf-8", errors="replace")
        active = [line for line in main_menu.splitlines() if MENU_WINDOW in line.split("#", 1)[0]]
        checks.check(f"vanilla does not define {MENU_WINDOW} (it is commented out)", ok=not active)
    else:
        print(f"SKIP vanilla comparison: {vanilla} not found")


def check_content(checks: Checks) -> None:
    """No gameplay content; player descriptions present and in the house voice."""
    for folder in GAMEPLAY_FOLDERS:
        checks.check(
            f"no {folder}/ folder (switch mod carries no gameplay)", ok=not (MOD / folder).exists()
        )
    allowed = {"descriptor.mod", "thumbnail.png", "interface", "localisation"}
    checks.check(
        "mod folder holds only the expected entries", ok={p.name for p in MOD.iterdir()} == allowed
    )
    for language in ("en", "pl"):
        text = (HERE / f"workshop-description.{language}.txt").read_text(encoding="utf-8")
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
    check_indicator(checks, args.game)
    check_content(checks)
    print(f"switch mod checks: {checks.passed} passed, {len(checks.failures)} failed")
    return 0 if not checks.failures else 1


if __name__ == "__main__":
    sys.exit(main())
