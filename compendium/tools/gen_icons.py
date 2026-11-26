#!/usr/bin/env python3
"""Makes the app's icons from a dump of the game (1.6.0): the item icons of every ingredient and material in
cook-data.txt (UI/StockItem/<actor>.sbitemico), the dishes (Item_Cook_*) once more at their full size for the Dishes
tab, the armor and the Ancient Oven items for the Reserve tab (an upgraded piece has no icon of its own: it takes the
one of the piece it is made from), and a few icons of the game's interface (Layout/Common.sblarc in
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


ENEMY_SIZE = 96


def enemy_names(cook_data):
    names = set()
    for line in Path(cook_data).read_text(encoding="utf-8").splitlines():
        parts = line.split(" ")
        if parts[0] == "LEVELFLAG" and len(parts) > 2:
            names.add(parts[2])
        elif parts[0] == "LEVELSPECIES":
            names.update(parts[3::2])
    return sorted(names)


def item_names(cook_data):
    names = set()
    for line in Path(cook_data).read_text(encoding="utf-8").splitlines():
        parts = line.split(" ")
        if parts[0] in ("ACTOR", "ITEM") and len(parts) > 2:
            names.add(parts[2])
    return sorted(names)


# An elixir (Item_Cook_C_17) has one picture per effect in the game's pouch; the actor's own is a placeholder.
ELIXIR_EFFECTS = ["AllSpeed", "AttackUp", "DefenseUp", "ExGutsMaxUp", "Fireproof", "GutsRecover", "LifeMaxUp",
                  "Quietness", "ResistCold", "ResistElectric", "ResistHot"]


def armor_icons(cook_data):
    """Each armor of cook-data.txt and where to look for its icon, in order: its own, then each piece below it (the
    piece one star down is among its ingredients); a "_B" look also without the "_B"."""
    armors = {}
    for line in Path(cook_data).read_text(encoding="utf-8").splitlines():
        parts = line.split(" ")
        if parts[0] != "ARMOR" or len(parts) < 9:
            continue
        count = int(parts[8])
        armors[parts[1]] = (parts[2], [parts[9 + 2 * i] for i in range(count)])
    result = {}
    for key, (name, _) in armors.items():
        chain, current, seen = [name], key, {key}
        while True:
            below = [i for i in armors[current][1] if i in armors and i not in seen]
            if not below:
                break
            current = below[0]
            seen.add(current)
            chain.append(armors[current][0])
        candidates = []
        for c in chain:
            for n in ([c, c[:-2]] if c.endswith("_B") else [c]):
                candidates.append(n)
                # The Gerudo jewelry's icon is under another name, its itemUseIconActorName in ActorInfo:
                # Armor_028_Head (Opal Earrings) is shown as Obj_Head_028.
                parts = n.split("_")
                if len(parts) == 3 and parts[0] == "Armor" and parts[1].isdigit():
                    candidates.append("Obj_%s_%s" % (parts[2], parts[1]))
        result[name] = candidates
    return result


def craft_names(cook_data):
    return sorted(line.split(" ")[2] for line in Path(cook_data).read_text(encoding="utf-8").splitlines()
                  if line.startswith("CRAFT "))


def main():
    romfs, cook_data, out = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    (out / "items").mkdir(parents=True, exist_ok=True)
    (out / "ui").mkdir(parents=True, exist_ok=True)
    (out / "dishes").mkdir(parents=True, exist_ok=True)

    made = missing = dishes = 0
    names = item_names(cook_data)
    if "Item_Cook_C_17" in names:
        names += ["Item_Cook_C_17_" + effect for effect in ELIXIR_EFFECTS]
    names += craft_names(cook_data)
    # The armor: each piece under its own name, with the first icon the dump has along its chain
    sources = {}
    for name, candidates in armor_icons(cook_data).items():
        for c in candidates:
            if (romfs / "UI/StockItem" / (c + ".sbitemico")).exists():
                sources[name] = c
                break
    armor = 0
    for name, source in sorted(sources.items()):
        textures = bntx.textures(bytes(oead.yaz0.decompress((romfs / "UI/StockItem" / (source + ".sbitemico")).read_bytes())))
        if not textures:
            continue
        _, w, h, rgba = textures[0]
        image = Image.frombytes("RGBA", (w, h), rgba)
        if max(w, h) > ITEM_SIZE:
            image = image.resize((ITEM_SIZE * w // max(w, h), ITEM_SIZE * h // max(w, h)), Image.LANCZOS)
        image.save(out / "items" / (name + ".png"), optimize=True)
        armor += 1
    for name in names:
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
        if name.startswith("Item_Cook_"):
            image.save(out / "dishes" / (name + ".png"), optimize=True)  # 144 px: the Dishes tab draws it large
            dishes += 1
        if max(w, h) > ITEM_SIZE:
            image = image.resize((ITEM_SIZE * w // max(w, h), ITEM_SIZE * h // max(w, h)), Image.LANCZOS)
        image.save(out / "items" / (name + ".png"), optimize=True)
        made += 1

    # Enemies of the EXP tab: their picture in the Hyrule Compendium (UI/PictureBook/<actor>.jpg, 280 px).
    (out / "enemies").mkdir(parents=True, exist_ok=True)
    enemies = 0
    for name in enemy_names(cook_data):
        path = romfs / "UI/PictureBook" / (name + ".jpg")
        if path.exists():
            Image.open(path).convert("RGB").resize((ENEMY_SIZE, ENEMY_SIZE), Image.LANCZOS).save(
                out / "enemies" / (name + ".jpg"), quality=90)
            enemies += 1

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
    print(f"{out}: {armor} armor icons, {made} item icons ({missing} items without one), {dishes} of them dishes at full size, "
          f"{len(found)} interface icons, {enemies} enemy pictures")


if __name__ == "__main__":
    main()
