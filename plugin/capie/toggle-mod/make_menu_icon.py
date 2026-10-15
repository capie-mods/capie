"""Draws the CAPIE main-menu icon (capie/gfx/interface/capie_menu_mark.dds) from the Overprint mark.

The geometry is the Overprint mark of sites/shared/theme/marks/capie-mark.svg (500 x 500
viewBox): a perforated parchment stamp, an olive panel with a khaki frame and rule, and the
vermilion and brass overprint turned by -7 degrees. Colours come from docs/design/tokens.json.
The stamp is cut out on a transparent background so it sits on the menu art like the vanilla
DLC medals, which share its 50 px slot.

Two frames side by side (100 x 50, uncompressed 32-bit RGBA DDS like vanilla interface sprites):
  1. the mark in colour: CAPIE is part of this playset (the frame the menu shows);
  2. the mark in ash greys with an empty caution-orange box (like an unticked DLC): "not loaded".
Rendered at 4x and reduced with Lanczos. No gradients, glow or shadows.

Usage:
    python plugin/capie/toggle-mod/make_menu_icon.py            # writes the DDS
    python plugin/capie/toggle-mod/make_menu_icon.py --check    # exit 1 if it differs
    python plugin/capie/toggle-mod/make_menu_icon.py --preview out.png  # 4x preview, both frames
"""

from __future__ import annotations

import argparse
import io
import json
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
TOKENS = REPO / "docs" / "design" / "tokens.json"
OUTPUT = HERE / "capie" / "gfx" / "interface" / "capie_menu_mark.dds"

FRAME = 50  # final frame edge in pixels (the DLC medal slot)
FRAMES = 2
SCALE = 4  # supersampling factor
VIEWBOX = 500.0  # source viewBox of the mark
STAMP_HEIGHT = 48.0  # final stamp height in pixels, inside the 50 px frame
PERF_RADIUS = 12.5
PERF_STEP_X = 33.5
PERF_STEP_Y = 32.8
OVERPRINT_ANGLE = -7.0
NOT_LOADED_ALPHA = 150  # the greyed frame is drawn less opaque than the active one
BOX_EDGE = 13  # empty state box of the second frame, final pixels
BOX_MARGIN = 1
BOX_STROKE = 2


def load_colors() -> dict[str, tuple[int, int, int]]:
    """Reads the colour tokens.

    Returns:
        Token name to RGB, for example "parchment" -> (236, 230, 211).
    """
    tokens = json.loads(TOKENS.read_text(encoding="utf-8"))
    colors: dict[str, tuple[int, int, int]] = {}
    for group in tokens["color"].values():
        for name, token in group.items():
            value = token["$value"].lstrip("#")
            colors[name] = (int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16))
    return colors


class Mark:
    """Maps the 500 px viewBox of the mark onto one supersampled frame."""

    def __init__(self, left: float) -> None:
        """Places the stamp centred in the frame that starts at `left` (supersampled px)."""
        self.unit = STAMP_HEIGHT * SCALE / (414 - 86 + 2 * PERF_RADIUS)
        self.left = left + FRAME * SCALE / 2 - 250 * self.unit
        self.top = FRAME * SCALE / 2 - 250 * self.unit

    def point(self, x: float, y: float) -> tuple[float, float]:
        """Converts a viewBox point.

        Returns:
            The point on the supersampled canvas.
        """
        return (self.left + x * self.unit, self.top + y * self.unit)

    def box(
        self, x: float, y: float, width: float, height: float
    ) -> tuple[float, float, float, float]:
        """Converts a viewBox rectangle.

        Returns:
            (left, top, right, bottom) on the supersampled canvas.
        """
        x0, y0 = self.point(x, y)
        return (x0, y0, x0 + width * self.unit, y0 + height * self.unit)

    def rotated(self, x: float, y: float, width: float, height: float) -> list[tuple[float, float]]:
        """Converts a rectangle of the overprint group (turned around the centre).

        Returns:
            Its four corners on the supersampled canvas.
        """
        angle = math.radians(OVERPRINT_ANGLE)
        corners = [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]
        result = []
        for cx, cy in corners:
            dx, dy = cx - 250, cy - 250
            rx = 250 + dx * math.cos(angle) - dy * math.sin(angle)
            ry = 250 + dx * math.sin(angle) + dy * math.cos(angle)
            result.append(self.point(rx, ry))
        return result


def draw_mark(canvas: Image.Image, left: float, palette: dict[str, tuple[int, int, int]]) -> None:
    """Draws one stamp into the frame that starts at `left`."""
    mark = Mark(left)
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    pen = ImageDraw.Draw(layer)
    pen.rectangle(mark.box(116, 86, 268, 328), fill=(*palette["parchment"], 255))
    radius = PERF_RADIUS * mark.unit
    perforations: list[tuple[float, float]] = [
        (116 + i * PERF_STEP_X, y) for i in range(9) for y in (86, 414)
    ]
    perforations += [(x, 86 + i * PERF_STEP_Y) for i in range(1, 10) for x in (116, 384)]
    for x, y in perforations:  # cut the perforations out: the menu art shows through
        cx, cy = mark.point(x, y)
        pen.ellipse((cx - radius, cy - radius, cx + radius, cy + radius), fill=(0, 0, 0, 0))
    pen.rectangle(mark.box(142, 112, 216, 276), fill=(*palette["olive"], 255))
    for rect in ((153, 123, 194, 5), (153, 372, 194, 5), (153, 123, 5, 254), (342, 123, 5, 254)):
        pen.rectangle(mark.box(*rect), fill=(*palette["khaki"], 255))
    pen.rectangle(mark.box(168, 318, 164, 22), fill=(*palette["khaki"], 255))
    for rect, color in (
        ((120, 307.5, 260, 15), palette["vermilion"]),
        ((120, 335.5, 260, 15), palette["vermilion"]),
        ((162, 221.5, 176, 46), palette["brass"]),
    ):
        pen.polygon(mark.rotated(*rect), fill=(*color, 255))
    canvas.alpha_composite(layer)


def grey(palette: dict[str, tuple[int, int, int]]) -> dict[str, tuple[int, int, int]]:
    """The "not loaded" palette: every mark colour mapped to an ash or steel tone.

    Returns:
        The palette for the second frame.
    """
    return {
        **palette,
        "parchment": palette["ash"],
        "olive": palette["steel-shadow"],
        "khaki": palette["steel-bevel"],
        "vermilion": palette["steel-bevel"],
        "brass": palette["steel-bevel"],
    }


def draw(palette: dict[str, tuple[int, int, int]]) -> Image.Image:
    """Draws both frames.

    Returns:
        The 100 x 50 RGBA sprite strip.
    """
    colors = {**palette, "olive": palette["olive-button"]}
    big = Image.new("RGBA", (FRAME * FRAMES * SCALE, FRAME * SCALE), (0, 0, 0, 0))
    draw_mark(big, 0, colors)
    second = Image.new("RGBA", big.size, (0, 0, 0, 0))
    draw_mark(second, FRAME * SCALE, grey(colors))
    alpha = second.getchannel("A").point(lambda value: value * NOT_LOADED_ALPHA // 255)
    second.putalpha(alpha)
    big.alpha_composite(second)
    # "Not loaded": an empty box like the unticked vanilla DLC box, in caution orange.
    box = ImageDraw.Draw(big)
    edge = BOX_EDGE * SCALE
    left = FRAME * SCALE + (FRAME - BOX_EDGE - BOX_MARGIN) * SCALE
    top = (FRAME - BOX_EDGE - BOX_MARGIN) * SCALE
    box.rectangle(
        (left, top, left + edge, top + edge),
        fill=(*palette["field-ink"], 255),
        outline=(*palette["caution-orange"], 255),
        width=BOX_STROKE * SCALE,
    )
    return big.resize((FRAME * FRAMES, FRAME), Image.Resampling.LANCZOS)


def encode(image: Image.Image) -> bytes:
    """Encodes the sprite strip as uncompressed 32-bit RGBA DDS.

    Returns:
        DDS bytes.
    """
    buffer = io.BytesIO()
    image.save(buffer, format="DDS")
    return buffer.getvalue()


def main() -> int:
    """Writes, checks or previews the icon.

    Returns:
        The process exit code.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check", action="store_true", help="exit 1 when the DDS differs from a fresh render"
    )
    parser.add_argument("--preview", type=Path, help="also write a 4x PNG preview of both frames")
    args = parser.parse_args()
    image = draw(load_colors())
    data = encode(image)
    if args.preview:
        image.resize((image.width * SCALE, image.height * SCALE), Image.Resampling.NEAREST).save(
            args.preview
        )
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
