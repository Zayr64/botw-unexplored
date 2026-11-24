#!/usr/bin/env python3
"""Pictures of the EXP tab's enemies from Zelda Wiki: renders of each enemy's model on a transparent background,
one per colour (zeldawiki.wiki, files "BotW_<English name>_Model.png"). The user chose them over the Hyrule
Compendium photos on 24/11/2026. They are not in this repository: the list of their addresses and the images stay
on the PC, and the images go to sdmc:/switch/botw-compendium/icons/enemies/ (as <actor>.png, which the app prefers to
the <actor>.jpg made by gen_icons.py from the game's photos).

Usage: python get_enemy_models.py <actor -> url .json> <download folder> <icons folder>
Needs Pillow.
"""
import json
import sys
import time
import urllib.request
from pathlib import Path

from PIL import Image

SIZE = 96


def main():
    urls = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    raw, out = Path(sys.argv[2]), Path(sys.argv[3]) / "enemies"
    raw.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    done = 0
    for actor, url in sorted(urls.items()):
        path = raw / (actor + ".png")
        if not path.exists():
            request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (personal icon cache)"})
            with urllib.request.urlopen(request, timeout=30) as response:
                path.write_bytes(response.read())
            time.sleep(0.5)
        image = Image.open(path).convert("RGBA")
        box = image.getbbox()  # trim the empty margins, then fit in a square
        if box:
            image = image.crop(box)
        w, h = image.size
        scale = SIZE / max(w, h)
        image = image.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.LANCZOS)
        square = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
        square.alpha_composite(image, ((SIZE - image.width) // 2, (SIZE - image.height) // 2))
        square.save(out / (actor + ".png"), optimize=True)
        done += 1
    print(f"{out}: {done} enemy models")


if __name__ == "__main__":
    main()
