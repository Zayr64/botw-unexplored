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
    "AttackUp": "Nt_PauseIconAttackCook_00",
    "DefenseUp": "Nt_PauseIconShieldCook_00",
    "MovingSpeed": "Nt_PauseIconGoGo_00",
    "Quietness": "Nt_PauseIconNoise_00",
    "ResistHot": "Nt_PauseIconCool_00",
    "ResistCold": "Nt_PauseIconHot_00",
    "ResistElectric": "Nt_PauseIconElect_00",
    "Fireproof": "Nt_PauseIconFire_00",
    "GutsRecover": "Nt_PauseIconGanbariRe_00",
    "ExGutsMaxUp": "Nt_PauseIconGanbari_00",  # a wheel; the app tints it yellow like the extra wheels
}


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
            found[texture].save(out / "ui" / (app_name + ".png"), optimize=True)
        else:
            print("not found:", texture)
    print(f"{out}: {made} item icons ({missing} items without one), {len(found)} interface icons")


if __name__ == "__main__":
    main()
