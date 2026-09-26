"""Builds the adaptive launcher icon from the upstream docs/icon.png.

The upstream icon is the "xRA" lettering on a dark rounded square. Android
draws its own mask, so only the lettering is kept: it becomes the
foreground layer (and, white, the monochrome layer for themed icons), and
the square's colour becomes the background in res/values/themes.xml.

    python android/tools/make_icons.py      (needs Pillow)
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "docs" / "icon.png"
RES = ROOT / "android" / "app" / "src" / "main" / "res"

BACKGROUND = (10, 16, 32)  # icon_background in themes.xml
# 108 dp canvas; the lettering spans 60 dp so its corners stay inside the
# 66 dp circle every launcher mask keeps.
CANVAS_DP = 108
LETTERING_DP = 60
DENSITIES = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}


def lettering(icon: Image.Image) -> Image.Image:
    """The pixels that are neither transparent nor the square's colour."""
    out = Image.new("RGBA", icon.size, (0, 0, 0, 0))
    src, dst = icon.load(), out.load()
    for y in range(icon.height):
        for x in range(icon.width):
            r, g, b, a = src[x, y]
            far = abs(r - BACKGROUND[0]) + abs(g - BACKGROUND[1]) + abs(b - BACKGROUND[2])
            if a > 0 and far > 60:
                dst[x, y] = (r, g, b, a)
    return out.crop(out.getbbox())


def main() -> None:
    letters = lettering(Image.open(SOURCE).convert("RGBA"))
    white = Image.new("RGBA", letters.size, (255, 255, 255, 255))
    white.putalpha(letters.getchannel("A"))

    for name, scale in DENSITIES.items():
        size = round(CANVAS_DP * scale)
        width = round(LETTERING_DP * scale)
        height = round(letters.height * width / letters.width)
        folder = RES / f"mipmap-{name}"
        folder.mkdir(parents=True, exist_ok=True)
        for layer, image in (("foreground", letters), ("monochrome", white)):
            canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
            scaled = image.resize((width, height), Image.LANCZOS)
            canvas.paste(scaled, ((size - width) // 2, (size - height) // 2), scaled)
            canvas.save(folder / f"ic_launcher_{layer}.png", optimize=True)
        print(f"{name}: {size}px, lettering {width}x{height}")


if __name__ == "__main__":
    main()
