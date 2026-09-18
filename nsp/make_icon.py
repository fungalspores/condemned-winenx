#!/usr/bin/env python3
"""Make the HOME menu icon (256x256 JPEG) from the game's own splash screens.

    make_icon.py [--art splash04|splash01|splash03|corridor] [--out nsp]

Writes icon.jpg (the game) and icon-setup.jpg (the same picture with a SETUP
band, for the setup program's NSP).

The splash screens are 4:3 pictures stored as 1024x1024 DXT textures in
global/ui of game/Game/CondemnedA.Arch00 (a 12-byte "TEXR" header before the
DDS). They are widened back to 4:3 and a square with the logo is cut out.
Needs Pillow. The icon is made from the player's own copy of the game and is not
part of the repository.
"""
import argparse, io, os, sys, zlib

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import ltar  # noqa: E402

ARCHIVE = os.path.join(ROOT, "game", "Game", "CondemnedA.Arch00")
# texture, left edge of the 1024x1024 square in the 1365x1024 (4:3) picture
ART = {
    "splash04": ("splashimage04", 90),    # the gun, the detective, the logo
    "splash01": ("splashimage01", 300),   # Ethan and the taped-off door
    "splash03": ("splashimage03", 0),     # the body in the fog
    "corridor": ("splashimage02", 200),   # the dark corridor
}


def texture(name):
    f, entries = ltar.read_index(ARCHIVE)
    want = f"global\\ui\\{name}.dds".lower()
    for path, off, csize, size, comp in entries:
        if path.lower() == want:
            f.seek(off)
            data = f.read(csize)
            if comp:
                data = zlib.decompress(data)[:size]
            if data[:4] == b"TEXR":
                data = data[12:]
            return Image.open(io.BytesIO(data)).convert("RGB")
    sys.exit(f"нет {want} в {ARCHIVE}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--art", choices=sorted(ART), default="splash04")
    ap.add_argument("--out", default=os.path.join(ROOT, "nsp"), help="папка для icon.jpg и icon-setup.jpg")
    a = ap.parse_args()

    name, left = ART[a.art]
    picture = texture(name).resize((1365, 1024), Image.LANCZOS)
    icon = picture.crop((left, 0, left + 1024, 1024)).resize((256, 256), Image.LANCZOS)
    game = os.path.join(a.out, "icon.jpg")
    icon.save(game, "JPEG", quality=92)
    print(f"иконка: {game} ({a.art}, {os.path.getsize(game)} байт)")

    setup = icon.convert("RGBA")
    band = Image.new("RGBA", setup.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(band)
    draw.rectangle((0, 188, 256, 240), fill=(150, 10, 10, 225))
    font = ImageFont.load_default(size=38)
    width = draw.textlength("SETUP", font=font)
    draw.text(((256 - width) / 2, 193), "SETUP", font=font, fill=(255, 255, 255, 255))
    setup = Image.alpha_composite(setup, band).convert("RGB")
    path = os.path.join(a.out, "icon-setup.jpg")
    setup.save(path, "JPEG", quality=92)
    print(f"иконка настройки: {path} ({os.path.getsize(path)} байт)")


if __name__ == "__main__":
    main()
