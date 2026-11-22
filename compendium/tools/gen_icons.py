#!/usr/bin/env python3
"""Makes the app's icons from a dump of the game (1.6.0): the item icons of every ingredient and material in
cook-data.txt (UI/StockItem/<actor>.sbitemico) and a few icons of the game's interface (Layout/Common.sblarc in
Pack/Bootup.pack): rupee, heart, and the effect icons of the pause menu.

They are the game's images, so they never go into this repository: copy the output folder to
sdmc:/switch/botw-compendium/icons/ on the SD card.

Usage: python gen_icons.py <romfs folder> <cook-data.txt> <output folder>
Needs oead and Pillow.
"""
import sys
from pathlib import Path

import oead
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import bntx  # noqa: E402

ITEM_SIZE = 72  # the app draws them at 26 to 30 pixels; the texture gets mipmaps

# Name in the app -> texture of Common.sblarc (the part before "^").
UI_ICONS = {
    "rupee": "Nt_RupeeIconNew_00",
    "heart": "Nt_HeartNew_00",
    "clock": "Nt_ClockIcon_00",
    "AttackUp": "Nt_PauseIconAttackCook_00",
    "DefenseUp": "Nt_PauseIconShieldCook_00",
    "MovingSpeed": "Nt_PauseIconGoGo_00",
    "Quietness": "Nt_PauseIconNoise_00",
    # Heat resistance (a Chilly dish) shows the sun, cold resistance (Spicy) the snowflake: the thing resisted.
    "ResistHot": "Nt_PauseIconHot_00",
    "ResistCold": "Nt_PauseIconCool_00",
    "ResistElectric": "Nt_PauseIconElect_00",
    "Fireproof": "Nt_PauseIconFire_00",
    "GutsRecover": "Nt_PauseIconGanbariRe_00",
    "ExGutsMaxUp": "Nt_PauseIconGanbariRe_00",  # the same wheel, in the yellow of extra wheels
}

# The textures are grey shapes the game colours as it draws them. These are the colours of the effect icons below
# Link's hearts in the game, measured on Zelda Wiki's images of them (zeldawiki.wiki/wiki/Added_Effect, 22/11/2026);
# the small arrow of some icons is white, as in the game. Extra stamina has no image there: the stamina icon in the
# yellow of extra hearts, which the game also uses for the extra wheels. Icons not listed stay grey (the app tints
# them).
EFFECT_COLORS = {
    "AttackUp": (255, 255, 255),
    "DefenseUp": (255, 255, 255),
    "MovingSpeed": (10, 124, 252),
    "Quietness": (180, 28, 200),
    "ResistHot": (232, 132, 0),
    "ResistCold": (132, 248, 252),
    "ResistElectric": (236, 236, 0),
    "Fireproof": (244, 52, 48),
    "GutsRecover": (12, 252, 104),
    "ExGutsMaxUp": (240, 224, 0),
}


def colorize(image, color):
    """The game's grey masks: the shape is the first channel, the alpha the second. The game paints the full parts in
    the effect's colour and the empty parts that are still opaque (the small arrow) in white; in between it blends."""
    out = image.copy()
    px = out.load()
    w, h = out.size
    for y in range(h):
        for x in range(w):
            r, _g, _b, a = px[x, y]
            px[x, y] = tuple(255 + (c - 255) * r // 255 for c in color) + (a,)
    return out


def item_names(cook_data):
    names = set()
    for line in Path(cook_data).read_text(encoding="utf-8").splitlines():
        parts = line.split(" ")
        if parts[0] in ("ACTOR", "ITEM") and len(parts) > 2:
            names.add(parts[2])
    return sorted(names)


def main():
    romfs, cook_data, out = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    (out / "items").mkdir(parents=True, exist_ok=True)
    (out / "ui").mkdir(parents=True, exist_ok=True)

    made = missing = 0
    for name in item_names(cook_data):
        path = romfs / "UI/StockItem" / (name + ".sbitemico")
        if not path.exists():
            missing += 1
            continue
        textures = bntx.textures(bytes(oead.yaz0.decompress(path.read_bytes())))
        if not textures:
            missing += 1
            continue
        _, w, h, rgba = textures[0]
        image = Image.frombytes("RGBA", (w, h), rgba)
        if max(w, h) > ITEM_SIZE:
            image = image.resize((ITEM_SIZE * w // max(w, h), ITEM_SIZE * h // max(w, h)), Image.LANCZOS)
        image.save(out / "items" / (name + ".png"), optimize=True)
        made += 1

    pack = oead.Sarc((romfs / "Pack/Bootup.pack").read_bytes())
    layout = oead.Sarc(oead.yaz0.decompress(bytes(pack.get_file("Layout/Common.sblarc").data)))
    wanted = set(UI_ICONS.values())
    found = {}
    for f in layout.get_files():
        if f.name.endswith(".bntx"):
            for name, w, h, rgba in bntx.textures(bytes(f.data), lambda n: n.split("^")[0] in wanted):
                found[name.split("^")[0]] = Image.frombytes("RGBA", (w, h), rgba)
    for app_name, texture in UI_ICONS.items():
        if texture in found:
            image = found[texture]
            if app_name in EFFECT_COLORS:
                image = colorize(image, EFFECT_COLORS[app_name])
            image.save(out / "ui" / (app_name + ".png"), optimize=True)
        else:
            print("not found:", texture)
    print(f"{out}: {made} item icons ({missing} items without one), {len(found)} interface icons")


if __name__ == "__main__":
    main()
