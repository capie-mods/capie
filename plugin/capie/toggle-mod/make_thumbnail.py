"""Draws thumbnail.png of the "CAPIE" switch mod from docs/design/tokens.json.

The picture follows docs/design/DESIGN-SYSTEM.md and shares its layout with the Rename Anything
preview (tools/promo/promo.py, ``preview_canvas``): a flat panel-umber panel in a window-ink
frame, a real screenshot on top (a Rename Anything editor in the game's own text fields, the
thing CAPIE makes possible), and under it the CAPIE mark Overprint, the name in parchment and the
full name in khaki. The mark is read from capie-mark-detail.svg, the same geometry as the
capie-mods organisation mark, so it is never drawn a second time. Letters are sized to stay
readable at the 200 px Steam shows in lists. No gradients, glow or shadows.

Fonts: Segoe UI (part of the design system's fallback stack, present on every Windows
installation). Ubuntu is preferred by the system but is not installed on the build machine.

Usage:
    python plugin/capie/toggle-mod/make_thumbnail.py            # writes capie/thumbnail.png
    python plugin/capie/toggle-mod/make_thumbnail.py --check    # exit 1 if the file differs
"""

from __future__ import annotations

import argparse
import io
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
"""Folder of this script (``plugin/capie/toggle-mod``)."""

REPO = HERE.parents[2]
"""Repository root."""

OUTPUT = HERE / "capie" / "thumbnail.png"
"""The switch mod's preview image (``picture`` in its descriptor.mod)."""

sys.path.insert(0, str(REPO / "tools" / "promo"))
import promo  # noqa: E402 - the promo pipeline sits outside this folder

SCREENSHOT = promo.Shot("subject-editor", (30, 22, 426, 249))
"""A Rename Anything editor with two of the game's own text fields, cropped to 1.74:1."""


def load_colors() -> dict[str, str]:
    """Reads the colour tokens.

    Returns:
        Every colour token by name, for example "panel-umber" -> "#272421".
    """
    return promo.load_colors()


def draw(colors: dict[str, str]) -> Image.Image:
    """Draws the thumbnail.

    Args:
        colors: Colour tokens by name.

    Returns:
        The final 512 x 512 RGB image.
    """
    canvas, pen, s = promo.preview_canvas(SCREENSHOT, colors)
    stamp = promo.draw_overprint(pen, (36 * s, 314 * s), 162 * s, colors["panel-umber"])
    x = 36 * s + stamp + 26 * s
    pen.text(
        (x, 298 * s), "CAPIE", font=promo.font(promo.FONT_BOLD, 84 * s), fill=colors["parchment"]
    )
    face = promo.font(promo.FONT_REGULAR, 22 * s)
    pen.text((x + 3 * s, 398 * s), "Cyprian's API", font=face, fill=colors["khaki"])
    pen.text((x + 3 * s, 426 * s), "Extender", font=face, fill=colors["khaki"])
    return canvas.resize((promo.PREVIEW_SIZE, promo.PREVIEW_SIZE), Image.Resampling.LANCZOS)


def encode(image: Image.Image) -> bytes:
    """Encodes the image as PNG without timestamps or other variable chunks.

    Args:
        image: The final image.

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
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
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
