"""Draws thumbnail.png of the "CAPIE" switch mod from docs/design/tokens.json.

The picture follows docs/design/DESIGN-SYSTEM.md: a flat panel-umber panel in a window-ink
frame, the CAPIE connector mark (three brass bars beside a field-ink plate with a steel
bevel), the name in parchment, the full name in khaki and one hairline-separated line that
says what the switch does, with the "works today" mark. No gradients, glow or shadows.

Fonts: Segoe UI (part of the design system's fallback stack, present on every Windows
installation). Ubuntu is preferred by the system but is not installed on the build machine.

Usage:
    python plugin/capie/toggle-mod/make_thumbnail.py            # writes capie/thumbnail.png
    python plugin/capie/toggle-mod/make_thumbnail.py --check    # exit 1 if the file differs
"""

from __future__ import annotations

import argparse
import io
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
TOKENS = REPO / "docs" / "design" / "tokens.json"
OUTPUT = HERE / "capie" / "thumbnail.png"
FONT_DIR = Path("C:/Windows/Fonts")
FONT_BOLD = FONT_DIR / "segoeuib.ttf"
FONT_REGULAR = FONT_DIR / "segoeui.ttf"

SIZE = 512  # final edge in pixels (Workshop and Paradox Launcher preview)
SCALE = 4  # supersampling factor for smooth edges


def load_colors() -> dict[str, str]:
    """Reads the colour tokens.

    Returns:
        Every colour token by name, for example "panel-umber" -> "#272421".
    """
    tokens = json.loads(TOKENS.read_text(encoding="utf-8"))
    colors: dict[str, str] = {}
    for group in tokens["color"].values():
        for name, token in group.items():
            colors[name] = token["$value"]
    return colors


def px(value: float) -> int:
    """Converts a coordinate of the 512 px design grid to the supersampled canvas.

    Returns:
        The coordinate on the supersampled canvas.
    """
    return round(value * SCALE)


def box(x0: float, y0: float, x1: float, y1: float) -> tuple[int, int, int, int]:
    """Maps a rectangle of the design grid to the supersampled canvas.

    Returns:
        The rectangle as (left, top, right, bottom) canvas pixels.
    """
    return (px(x0), px(y0), px(x1), px(y1))


def font(path: Path, size: float) -> ImageFont.FreeTypeFont:
    """Loads a font at a design-grid size.

    Returns:
        The font scaled for the supersampled canvas.
    """
    return ImageFont.truetype(str(path), px(size))


def draw(colors: dict[str, str]) -> Image.Image:
    """Draws the thumbnail.

    Returns:
        The final 512 x 512 RGB image.
    """
    canvas = Image.new("RGB", (px(SIZE), px(SIZE)), colors["window-ink"])
    pen = ImageDraw.Draw(canvas)

    # Panel: umber field inside the window-ink frame, one rim hairline.
    pen.rectangle(box(12, 12, SIZE - 12, SIZE - 12), fill=colors["panel-umber"])
    pen.rectangle(box(12, 12, SIZE - 12, SIZE - 12), outline=colors["panel-rim"], width=px(2))

    # Connector mark: field-ink plate with a steel bevel, three brass bars on its left.
    plate = box(92, 106, 196, 266)
    pen.rounded_rectangle(
        plate, radius=px(2), fill=colors["field-ink"], outline=colors["steel-bevel"], width=px(3)
    )
    for top in (138, 178, 218):
        pen.rectangle(box(48, top, 92, top + 16), fill=colors["brass"])

    # Name and full name, left-aligned beside the mark.
    pen.text((px(226), px(110)), "CAPIE", font=font(FONT_BOLD, 84), fill=colors["parchment"])
    pen.text(
        (px(229), px(214)),
        "Cyprian's API Extender",
        font=font(FONT_REGULAR, 25),
        fill=colors["khaki"],
    )

    # What the switch does, under one hairline.
    pen.rectangle(box(48, 330, SIZE - 48, 332), fill=colors["panel-rim"])
    pen.rectangle(box(48, 372, 64, 388), fill=colors["pencil-green-lit"])
    body = font(FONT_REGULAR, 24)
    pen.text(
        (px(78), px(362)),
        "Enabled in the playset: CAPIE loads",
        font=body,
        fill=colors["parchment"],
    )
    pen.text(
        (px(78), px(400)), "Disabled: the game starts without it", font=body, fill=colors["ash"]
    )

    return canvas.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def encode(image: Image.Image) -> bytes:
    """Encodes the image as PNG without timestamps or other variable chunks.

    Returns:
        Reproducible PNG bytes.
    """
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def main() -> int:
    """Writes or checks thumbnail.png.

    Returns:
        The process exit code.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check", action="store_true", help="exit 1 when thumbnail.png differs from a fresh render"
    )
    args = parser.parse_args()
    data = encode(draw(load_colors()))
    if args.check:
        same = OUTPUT.exists() and OUTPUT.read_bytes() == data
        print("UP-TO-DATE" if same else "STALE", OUTPUT)
        return 0 if same else 1
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(data)
    print(f"Wrote {OUTPUT} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
