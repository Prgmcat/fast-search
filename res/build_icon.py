"""
Build a multi-resolution Windows .ico from the raw generated PNG.

Steps:
  1. Load raw PNG.
  2. Auto-crop the centered icon by finding non-white pixels (tolerant to a
     near-white letterbox the image generator adds around the rounded-square
     app icon).
  3. Pad to a square with a transparent background.
  4. Resize to the standard Windows icon sizes.
  5. Save as .ico with all sizes embedded.
"""

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


def make_transparent_outside_rounded_rect(im: Image.Image) -> Image.Image:
    """The original image already has a rounded-square icon drawn on a white
    card. We try to keep the card but turn the white outside into transparent.
    After cropping we should already be on the icon body, so just ensure the
    alpha channel is set from non-white pixels at the border."""
    rgba = im.convert("RGBA")
    return rgba


def main() -> None:
    raw = Image.open(SRC)
    cropped = trim_near_white(raw, threshold=235)
    squared = pad_to_square(cropped)

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
