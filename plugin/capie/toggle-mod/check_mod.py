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
import hashlib
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

LAUNCHER_CONFIG = REPO / "plugin" / "capie" / "src" / "launcher" / "config.hpp"
"""Launcher source that compiles in the descriptor path it looks for."""

SDK_LOCALISATION = REPO / "sdk" / "capie-script-sdk" / "mod" / "localisation"
"""Localisation of the CAPIE script SDK; the status fallback must match it."""

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

UTF8_BOM = b"\xef\xbb\xbf"
"""Byte order mark HOI4 needs at the start of localisation files."""

GAMEPLAY_FOLDERS = ("common", "events", "history", "map", "music", "sound", "portraits")
"""Folders that would carry gameplay content; the switch mod has none of them."""

MENU_FILE = "frontendmainview.gui"
MENU_WINDOW = "mainmenu_panel_upperleft"  # created by name by hoi4.exe (frontendmainview.cpp)
MENU_ELEMENT = "capie_menu_mark"
MENU_SPRITE = "GFX_capie_menu_mark"
MENU_TOOLTIP = "CAPIE_MENU_MARK_TT"
MENU_GFX = "capie_menu.gfx"
MENU_TEXTURE = "gfx/interface/capie_menu_mark.dds"
MENU_FRAME_EDGE = 50  # one frame, the size of a vanilla DLC medal slot
MENU_FRAMES = 2
DDS_MAGIC = b"DDS "
DDS_HEIGHT_OFFSET = 12
DDS_WIDTH_OFFSET = 16
DDS_BITCOUNT_OFFSET = 88
DDS_RGBA_BITS = 32
FALLBACK_KEYS = ("CAPIE_STATUS", "CAPIE_API", "CAPIE_BUILD")
BLOCK_BEGIN = "# CAPIE BEGIN"
BLOCK_END = "# CAPIE END"
# SHA-256 of interface/frontendmainview.gui of HOI4 1.19.3 (17 757 bytes). A game patch that
# changes the file makes the check fail: re-create the override from the new file.
VANILLA_MENU_SHA256 = "17E5F3CDE42D170B9E4AC40BB2312CD2D505767920B6E995C5C58B48C38EFD1B"
"""Front-end window name the engine looks up for the main-menu stamp."""

STATUS_KEY = "CAPIE_STATUS"
"""Localisation key CAPIE answers with its version."""

POLISH_GLYPH_FONTS = ("hoi_18", "hoi_18mbs", "hoi_16mbs", "Arial12")
"""Game fonts with complete Polish glyphs (design system typography)."""

LANGUAGES = ("english", "polish")
"""Languages the switch mod ships localisation for."""

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


def balanced(text: str) -> bool:
    """Checks that braces are balanced outside comments and strings.

    Args:
        text: Paradox script or GUI text.

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

    Args:
        path: Localisation file.
        key: Localisation key.

    Returns:
        The quoted text, or None when the key is missing.
    """
    text = path.read_bytes().removeprefix(UTF8_BOM).decode("utf-8")
    match = re.search(rf'^\s*{re.escape(key)}:\d*\s+"(.*)"\s*$', text, flags=re.MULTILINE)
    return str(match.group(1)) if match else None


def check_descriptors(checks: Checks) -> None:
    """Descriptor fields, the local registration file and the launcher identity.

    Args:
        checks: Result collector.
    """
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


def strip_capie_block(text: str) -> tuple[str, str]:
    """Splits the menu override into the vanilla text and the CAPIE block.

    Returns:
        (text without the lines from CAPIE BEGIN to CAPIE END, the block itself); the block
        is empty when the markers are missing or not in order.
    """
    lines = text.splitlines(keepends=True)
    begin = [i for i, line in enumerate(lines) if line.strip().startswith(BLOCK_BEGIN)]
    end = [i for i, line in enumerate(lines) if line.strip() == BLOCK_END]
    if len(begin) != 1 or len(end) != 1 or end[0] < begin[0]:
        return text, ""
    return "".join(lines[: begin[0]] + lines[end[0] + 1 :]), "".join(lines[begin[0] : end[0] + 1])


def window_of(text: str, position: int) -> str:
    """Names the containerWindowType that encloses a position (by brace depth).

    Returns:
        The window name, or an empty string.
    """
    depth = 0
    stack: list[tuple[int, str]] = []
    pending = ""
    for match in re.finditer(
        r'#[^\n]*|"[^"]*"|\{|\}|containerWindowType|name\s*=\s*"([^"]*)"', text
    ):
        if match.start() >= position:
            break
        lexeme = match.group(0)
        if lexeme.startswith("#"):
            continue
        if lexeme == "containerWindowType":
            pending = "window"
        elif lexeme == "{":
            depth += 1
            if pending == "window":
                stack.append((depth, ""))
                pending = ""
        elif lexeme == "}":
            while stack and stack[-1][0] >= depth:
                stack.pop()
            depth -= 1
        elif match.group(1) is not None and stack and stack[-1][0] == depth and not stack[-1][1]:
            stack[-1] = (depth, match.group(1))
    return stack[-1][1] if stack else ""


def check_menu_sprite(checks: Checks) -> None:
    """The 2-frame sprite of the menu mark: .gfx definition, DDS format and freshness.

    Args:
        checks: Result collector.
    """
    gfx_files = sorted((MOD / "interface").glob("*.gfx"))
    checks.check("one .gfx file", ok=[f.name for f in gfx_files] == [MENU_GFX])
    gfx = (MOD / "interface" / MENU_GFX).read_text(encoding="utf-8")
    checks.check(".gfx braces are balanced", ok=balanced(gfx))
    checks.check(f".gfx defines {MENU_SPRITE}", ok=f'name = "{MENU_SPRITE}"' in gfx)
    checks.check(f".gfx points at {MENU_TEXTURE}", ok=f'texturefile = "{MENU_TEXTURE}"' in gfx)
    checks.check(".gfx declares two frames", ok=f"noOfFrames = {MENU_FRAMES}" in gfx)
    data = (MOD / MENU_TEXTURE).read_bytes()
    checks.check("sprite is a DDS file", ok=data.startswith(DDS_MAGIC))
    if len(data) > DDS_BITCOUNT_OFFSET + 4:
        height = int.from_bytes(data[DDS_HEIGHT_OFFSET : DDS_HEIGHT_OFFSET + 4], "little")
        width = int.from_bytes(data[DDS_WIDTH_OFFSET : DDS_WIDTH_OFFSET + 4], "little")
        bits = int.from_bytes(data[DDS_BITCOUNT_OFFSET : DDS_BITCOUNT_OFFSET + 4], "little")
        checks.check(
            "sprite is two 50 x 50 frames side by side",
            ok=(width, height) == (MENU_FRAME_EDGE * MENU_FRAMES, MENU_FRAME_EDGE),
        )
        checks.check("sprite is uncompressed 32-bit RGBA", ok=bits == DDS_RGBA_BITS)
    sys.path.insert(0, str(HERE))
    try:
        import make_menu_icon  # noqa: PLC0415 - local generator next to this file

        fresh = make_menu_icon.encode(make_menu_icon.draw(make_menu_icon.load_colors()))
        checks.check("sprite matches make_menu_icon.py and tokens.json", ok=fresh == data)
    finally:
        sys.path.pop(0)


def check_indicator(checks: Checks, game: Path) -> None:
    """Main-menu indicator (minimal override of the vanilla menu file) and its fallback text.

    Args:
        checks: Result collector.
        game: HOI4 installation for the vanilla comparison.
    """
    gui_files = sorted((MOD / "interface").glob("*.gui"))
    checks.check(
        "the only .gui file is the menu override", ok=[f.name for f in gui_files] == [MENU_FILE]
    )
    raw = (MOD / "interface" / MENU_FILE).read_bytes()
    text = raw.decode("utf-8")
    stripped, block = strip_capie_block(text)
    checks.check("override has one CAPIE BEGIN/END block", ok=bool(block))
    stripped_hash = hashlib.sha256(stripped.encode("utf-8")).hexdigest().upper()
    checks.check(
        "override minus the CAPIE block is the pinned vanilla 1.19.3 file",
        ok=stripped_hash == VANILLA_MENU_SHA256,
    )
    checks.check("override braces are balanced", ok=balanced(text))
    checks.check("CAPIE block braces are balanced", ok=bool(block) and balanced(block))
    checks.check(f'block names "{MENU_ELEMENT}"', ok=f'name = "{MENU_ELEMENT}"' in block)
    checks.check(f"block uses {MENU_SPRITE}", ok=f'spriteType = "{MENU_SPRITE}"' in block)
    checks.check("block shows frame 1 (CAPIE in this playset)", ok="frame = 1" in block)
    checks.check(f"block tooltip is {MENU_TOOLTIP}", ok=f'pdx_tooltip = "{MENU_TOOLTIP}"' in block)
    at = text.find(BLOCK_BEGIN)
    checks.check(
        f"block sits inside {MENU_WINDOW}, a window the game creates",
        ok=at >= 0 and window_of(text, at) == MENU_WINDOW,
    )
    check_menu_sprite(checks)
    for language in LANGUAGES:
        ours = MOD / "localisation" / language / f"capie_switch_l_{language}.yml"
        sdk = SDK_LOCALISATION / language / f"capie_sdk_v1_l_{language}.yml"
        loc = ours.read_bytes()
        checks.check(
            f"{language}: localisation has the UTF-8 BOM HOI4 needs", ok=loc.startswith(UTF8_BOM)
        )
        header = loc.removeprefix(UTF8_BOM).decode("utf-8").splitlines()[0]
        checks.check(f"{language}: header l_{language}:", ok=header == f"l_{language}:")
        for key in FALLBACK_KEYS:
            value = localisation_value(ours, key)
            checks.check(f"{language}: {key} fallback present", ok=value is not None)
            checks.check(
                f"{language}: {key} fallback equals the SDK text",
                ok=value == localisation_value(sdk, key),
            )
        tooltip = localisation_value(ours, MENU_TOOLTIP) or ""
        checks.check(
            f"{language}: {MENU_TOOLTIP} names status, API level and build",
            ok=all(f"${key}$" in tooltip for key in FALLBACK_KEYS),
        )
    vanilla = game / "interface" / MENU_FILE
    if vanilla.is_file():
        current = vanilla.read_bytes()
        checks.check(
            "installed game still has the pinned vanilla menu file (else re-create the override)",
            ok=hashlib.sha256(current).hexdigest().upper() == VANILLA_MENU_SHA256,
        )
        checks.check(
            "override minus the block equals the installed vanilla file byte for byte",
            ok=stripped.encode("utf-8") == current,
        )
        exe = game / "hoi4.exe"
        if exe.is_file():
            names = exe.read_bytes()
            checks.check(
                f"hoi4.exe looks up the window {MENU_WINDOW} by name",
                ok=MENU_WINDOW.encode("ascii") + b"\x00" in names,
            )
    else:
        print(f"SKIP vanilla comparison: {vanilla} not found")


def check_content(checks: Checks) -> None:
    """No gameplay content; player descriptions present and in the house voice.

    Args:
        checks: Result collector.
    """
    for folder in GAMEPLAY_FOLDERS:
        checks.check(
            f"no {folder}/ folder (switch mod carries no gameplay)", ok=not (MOD / folder).exists()
        )
    allowed = {"descriptor.mod", "thumbnail.png", "interface", "localisation", "gfx"}
    checks.check(
        "mod folder holds only the expected entries", ok={p.name for p in MOD.iterdir()} == allowed
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
    check_indicator(checks, args.game)
    check_content(checks)
    print(f"switch mod checks: {checks.passed} passed, {len(checks.failures)} failed")
    return 0 if not checks.failures else 1


if __name__ == "__main__":
    sys.exit(main())
