"""
Build a multi-resolution Windows .ico from the raw generated PNG.

Steps:
  1. Load raw PNG.
  2. Auto-crop the centered icon by finding non-white pixels (tolerant to a
     near-white letterbox the image generator adds around the rounded-square
     app icon).
  3. Knock out the white around the rounded-square corners (flood-fill from
     the four corners so only the pixels actually reachable via near-white
     paths turn transparent, preserving any in-image whites).
  4. Pad to a square with a transparent background.
  5. Resize to the standard Windows icon sizes.
  6. Save as .ico with all sizes embedded.
"""

from collections import deque
from pathlib import Path

from PIL import Image, ImageOps

HERE = Path(__file__).resolve().parent
SRC = HERE / "fastsearch_icon_raw.png"
OUT_ICO = HERE / "fastsearch.ico"
OUT_PNG = HERE / "fastsearch_icon.png"

SIZES = [16, 24, 32, 48, 64, 128, 256]


def trim_near_white(im: Image.Image, threshold: int = 235) -> Image.Image:
    """Crop away near-white letterboxing.

    A pixel is treated as "content" if any of its RGB channels is below
    ``threshold``. Getbbox of the resulting mask gives the content bounds.
    """
    rgba = im.convert("RGBA")
    r, g, b, _a = rgba.split()

    def below(c):
        return c.point(lambda v: 255 if v < threshold else 0)

    mask = Image.eval(
        Image.merge("RGB", [below(r), below(g), below(b)]).convert("L"),
        lambda v: 255 if v > 0 else 0,
    )
    bbox = mask.getbbox()
    if not bbox:
        return rgba
    return rgba.crop(bbox)


def pad_to_square(im: Image.Image) -> Image.Image:
    w, h = im.size
    side = max(w, h)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(im, ((side - w) // 2, (side - h) // 2), im if im.mode == "RGBA" else None)
    return canvas


def knockout_corners_white(im: Image.Image, hard: int = 232, soft: int = 190) -> Image.Image:
    """Flood-fill from each of the four image corners, turning near-white
    pixels transparent. Only pixels reachable from a corner via other
    near-white pixels get cleared, so any white pixel *inside* the icon body
    (e.g. the text/shape itself) is preserved.

    - pixels with min(r,g,b) >= ``hard``  -> alpha = 0 (fully transparent)
    - pixels between ``soft`` and ``hard`` -> partially transparent boundary
      (anti-aliased edge), fill does not propagate past them
    - pixels darker than ``soft`` are considered content and stop the fill
    """
    rgba = im.convert("RGBA").copy()
    w, h = rgba.size
    px = rgba.load()
    visited = [[False] * h for _ in range(w)]
    q: deque = deque()
    for x, y in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
        q.append((x, y))
    while q:
        x, y = q.popleft()
        if not (0 <= x < w and 0 <= y < h) or visited[x][y]:
            continue
        r, g, b, a = px[x, y]
        if a == 0:
            visited[x][y] = True
            q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
            continue
        v = min(r, g, b)
        if v >= hard:
            px[x, y] = (r, g, b, 0)
            visited[x][y] = True
            q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
        elif v >= soft:
            t = (v - soft) / (hard - soft)  # 0..1 (higher = closer to white)
            px[x, y] = (r, g, b, int(round(a * (1 - t))))
            visited[x][y] = True
            # do not propagate past the anti-aliased rim
    return rgba


def main() -> None:
    raw = Image.open(SRC)
    cropped = trim_near_white(raw, threshold=235)
    knocked = knockout_corners_white(cropped, hard=232, soft=190)
    squared = pad_to_square(knocked)

    # Save a reference PNG at 512px.
    ref = squared.resize((512, 512), Image.LANCZOS)
    ref.save(OUT_PNG, format="PNG")

    # Build multi-resolution ICO. Pillow's ICO writer resizes from the base
    # image once per entry in ``sizes`` (LANCZOS), so give it a 256x256
    # source for best quality across all embedded sizes.
    base = squared.resize((256, 256), Image.LANCZOS)
    base.save(OUT_ICO, format="ICO", sizes=[(s, s) for s in SIZES])

    print(f"Wrote {OUT_ICO} with sizes {SIZES}")
    print(f"Wrote reference PNG {OUT_PNG} (512x512)")


if __name__ == "__main__":
    main()
