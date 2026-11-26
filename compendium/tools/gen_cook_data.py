#!/usr/bin/env python3
"""Builds cook-data.txt, the cooking data the app reads from the SD card, from a dump of the game (1.6.0).

The data is the game's own (ActorInfo, Cooking/CookData, English names), so the output never goes into this
repository: it is generated on the PC from the user's dump and copied to the SD card.

Usage: python gen_cook_data.py <romfs folder> <output file>
Needs oead (pip install oead).
"""
import struct
import sys
import zlib
from pathlib import Path

import oead

# Effect names as the game's CookingMgr maps them (decomp: sCookingEffects and getCookEffectId).
EFFECT_IDS = {
    "LifeRecover": 1, "LifeMaxUp": 2, "ResistHot": 4, "ResistCold": 5, "ResistElectric": 6,
    "AttackUp": 10, "DefenseUp": 11, "Quietness": 12, "MovingSpeed": 13, "GutsRecover": 14,
    "ExGutsMaxUp": 15, "Fireproof": 16,
}
# CookData's System.CEI names its entries by these hashes (decomp: getCookEffectId).
CEI_NAMES = {
    "LifeRecover": 1, "GutsPerformance": 15, "StaminaRecover": 14, "LifeMaxUp": 2, "ResistHot": 4,
    "ResistCold": 5, "ResistElectric": 6, "AllSpeed": 13, "AttackUp": 10, "DefenseUp": 11,
    "Quietness": 12, "Fireproof": 16,
}
COOK_TAGS = ["CookFruit", "CookMushroom", "CookPlant", "CookMeat", "CookFish", "CookSpice", "CookInsect",
             "CookEnemy", "CookOre", "CookLowPrice", "CookFailure", "CookEMedicine"]


def crc(s):
    return zlib.crc32(s.encode()) & 0xFFFFFFFF


def f32_bits(v):
    return struct.unpack("<I", struct.pack("<f", float(v)))[0]


def num(v):
    return int(v) if v is not None else None


def msbt_names(data):
    """Labels and texts of an MSBT file, control codes dropped."""
    assert data[:8] == b"MsgStdBn"
    e = "<" if data[8:10] == b"\xff\xfe" else ">"
    enc = "utf-16-le" if e == "<" else "utf-16-be"
    nsec = struct.unpack_from(e + "H", data, 0x0E)[0]
    off, labels, texts = 0x20, {}, []
    for _ in range(nsec):
        magic, size = data[off:off + 4], struct.unpack_from(e + "I", data, off + 4)[0]
        body = off + 0x10
        if magic == b"LBL1":
            for i in range(struct.unpack_from(e + "I", data, body)[0]):
                cnt, loff = struct.unpack_from(e + "II", data, body + 4 + i * 8)
                p = body + loff
                for _ in range(cnt):
                    ln = data[p]
                    labels[data[p + 1:p + 1 + ln].decode("ascii")] = struct.unpack_from(e + "I", data, p + 1 + ln)[0]
                    p += 1 + ln + 4
        elif magic == b"TXT2":
            n = struct.unpack_from(e + "I", data, body)[0]
            offs = [struct.unpack_from(e + "I", data, body + 4 + i * 4)[0] for i in range(n)] + [size]
            for i in range(n):
                raw, out, j = data[body + offs[i]:body + offs[i + 1]], [], 0
                while j + 1 < len(raw):
                    c = struct.unpack_from(e + "H", raw, j)[0]
                    if c == 0x0E:
                        j += 8 + struct.unpack_from(e + "H", raw, j + 6)[0]
                        continue
                    if c == 0:
                        break
                    out.append(raw[j:j + 2].decode(enc, "replace"))
                    j += 2
                texts.append("".join(out))
        off = (body + size + 15) & ~15
    return {k: texts[v] for k, v in labels.items() if v < len(texts)}


def main():
    romfs, out_path = Path(sys.argv[1]), Path(sys.argv[2])
    info = oead.byml.from_binary(oead.yaz0.decompress((romfs / "Actor/ActorInfo.product.sbyml").read_bytes()))
    boot = oead.Sarc((romfs / "Pack/Bootup.pack").read_bytes())
    cook = None
    for f in boot.get_files():
        if f.name == "Cooking/CookData.sbyml":
            cook = oead.byml.from_binary(oead.yaz0.decompress(bytes(f.data)))
    names = {}
    effect_names = {}
    for f in oead.Sarc((romfs / "Pack/Bootup_USen.pack").read_bytes()).get_files():
        if f.name.endswith(".ssarc"):
            for m in oead.Sarc(oead.yaz0.decompress(bytes(f.data))).get_files():
                if m.name.startswith("ActorType/") and m.name.endswith(".msbt"):
                    for k, v in msbt_names(bytes(m.data)).items():
                        if k.endswith("_Name"):
                            names[k[:-5]] = v
                elif m.name == "StaticMsg/CookEffect.msbt":
                    # "<effect>_Name" is the word put before a dish's name ("Chilly ", "Mighty ").
                    for k, v in msbt_names(bytes(m.data)).items():
                        ids = dict(EFFECT_IDS, **CEI_NAMES)
                        if k.endswith("_Name") and k[:-5] in ids:
                            effect_names[ids[k[:-5]]] = v.strip()

    system = cook["System"]
    recipe_hashes, recipe_tags = set(), set()
    for r in list(cook["Recipes"]) + list(cook["SingleRecipes"]):
        recipe_hashes.add(int(r["Recipe"]))
        for slot in r.get("Actors", []):
            for h in (slot if isinstance(slot, oead.byml.Array) else [slot]):
                recipe_hashes.add(int(h))
        for slot in r.get("Tags", []):
            for h in (slot if isinstance(slot, oead.byml.Array) else [slot]):
                recipe_tags.add(int(h))
    wanted_tags = recipe_tags | {crc(t) for t in COOK_TAGS}
    special = {str(system["FA"]), str(system["FCA"]), str(system["MEA"])}

    lines = ["BOTW_COMPENDIUM_DATA 1",
             "# Generated from the game's files by compendium/tools/gen_cook_data.py. Do not share."]
    nmmr = [f32_bits(v) for v in system["NMMR"]]
    nmssr = [int(v) for v in system["NMSSR"]]
    lines.append("SYS %08x %08x %08x %08x %08x %d %d %d %s %s" % (
        crc(str(system["FA"])), crc(str(system["FCA"])), crc(str(system["MEA"])),
        f32_bits(system["LRMR"]), f32_bits(system["FALRMR"]), int(system["FALR"]), int(system["SFALR"]),
        int(system["SSAET"]), " ".join("%08x" % v for v in nmmr), " ".join(str(v) for v in nmssr)))
    cei_by_hash = {crc(n): i for n, i in CEI_NAMES.items()}
    for e in system["CEI"]:
        eid = cei_by_hash.get(int(e["T"]))
        if eid is None:
            continue
        lines.append("EFFECT %d %d %d %d %08x %d" % (eid, int(e["BT"]), int(e["Ma"]), int(e["Mi"]),
                                                    f32_bits(e["MR"]), int(e["SSA"])))

    count = 0
    for a in info["Actors"]:
        name = str(a["name"])
        h = crc(name)
        tags = [int(v) for v in a.get("tags", {}).values()] if "tags" in a else []
        if not (h in recipe_hashes or name in special or name.startswith("Item_Cook_")
                or any(t in wanted_tags for t in tags)):
            continue

        def opt(key):
            return "-" if key not in a else str(int(a[key]))

        eff = EFFECT_IDS.get(str(a.get("cureItemEffectType", "None")), -1)
        lines.append("ACTOR %08x %s %s %s %s %s %d %s %s %s %s %s %d %s" % (
            h, name, opt("itemSellingPrice"), opt("itemBuyingPrice"), opt("cureItemHitPointRecover"),
            opt("cureItemEffectLevel"), eff, opt("cookSpiceBoostEffectiveTime"),
            opt("cookSpiceBoostHitPointRecover"), opt("cookSpiceBoostMaxHeartLevel"),
            opt("cookSpiceBoostStaminaLevel"), opt("cookSpiceBoostSuccessRate"), len(tags),
            " ".join("%08x" % t for t in tags)))
        if name in names:
            lines.append("NAME %08x %s" % (h, names[name]))
        count += 1

    def hb_tb(r):
        return ("%d" % int(r["HB"]) if "HB" in r else "-") + " " + ("%d" % int(r["TB"]) if "TB" in r else "-")

    for r in cook["Recipes"]:
        parts = ["RECIPE %08x %s" % (int(r["Recipe"]), hb_tb(r))]
        for key in ("Actors", "Tags"):
            slots = list(r.get(key, []))
            parts.append(str(len(slots)))
            for slot in slots:
                parts.append(str(len(slot)) + " " + " ".join("%08x" % int(v) for v in slot))
        lines.append(" ".join(parts))
    for r in cook["SingleRecipes"]:
        parts = ["SINGLE %08x %s %d" % (int(r["Recipe"]), hb_tb(r), int(r["Num"]))]
        for key in ("Actors", "Tags"):
            vals = list(r.get(key, []))
            parts.append(str(len(vals)) + (" " + " ".join("%08x" % int(v) for v in vals) if vals else ""))
        lines.append(" ".join(parts))
    for eid, text in sorted(effect_names.items()):
        lines.append("EFFECTNAME %d %s" % (eid, text))
    for t in COOK_TAGS:
        lines.append("TAG %08x %s" % (crc(t), t))

    reserve_lines(info, romfs, names, {l.split(" ")[2] for l in lines if l.startswith("ACTOR ")}, out_path.parent, lines)
    level_lines(boot, names, lines)

    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{out_path}: {count} actors, {len(cook['Recipes'])} recipes, {len(cook['SingleRecipes'])} single recipes, "
          f"{sum(1 for l in lines if l.startswith('ARMOR '))} armors, {sum(1 for l in lines if l.startswith('CRAFT '))} "
          f"crafted items, {sum(1 for l in lines if l.startswith('QUEST '))} quest needs")


# Shops whose tables are not purchases of new items: the Great Fairies' upgrade lists, the shops that only sell back
# what the player already had (Tarrey Town), and the Ancient Oven and Robbie (made from materials, see CRAFT).
UPGRADE_SHOPS = ("Npc_DressFairy",)
REBUY_SHOPS = ("Npc_AncientAssistant003", "Npc_oasis050")
CRAFT_SHOPS = ("Npc_AncientDoctor", "TwnObj_AncientOven")


def scan_shops(romfs):
    """Item names sold new in a shop, and item names made at the Ancient Oven."""
    sold, crafted = set(), set()
    for p in sorted((romfs / "Actor/Pack").glob("*.sbactorpack")):
        if not p.name.startswith(("Npc_", "TwnObj_")) or p.stem.startswith(UPGRADE_SHOPS + REBUY_SHOPS):
            continue
        for f in oead.Sarc(oead.yaz0.decompress(p.read_bytes())).get_files():
            if not f.name.endswith(".bshop"):
                continue
            pio = oead.aamp.ParameterIO.from_binary(bytes(f.data))
            for name, obj in pio.objects.items():
                if name == "Header" or "ColumnNum" not in obj.params:
                    continue
                for i in range(1, int(obj.params["ColumnNum"].v) + 1):
                    key = "ItemName%03d" % i
                    if key in obj.params:
                        (crafted if p.stem.startswith(CRAFT_SHOPS) else sold).add(str(obj.params[key].v))
    return sold, crafted


def recipe_of(a):
    """The materials that make this actor (normal0 table: the Great Fairy's upgrade, or the Ancient Oven)."""
    out = []
    for i in range(1, int(a.get("normal0StuffNum", 0)) + 1):
        n, c = a.get("normal0ItemName%02d" % i), a.get("normal0ItemNum%02d" % i)
        if n is not None and c is not None and str(n):
            out.append((str(n), int(c)))
    return out


def use_icon_name(a, by_name):
    """Recipes name some materials by an alias (Item_Plant_J for Silent Princess); the pouch item is the one its
    itemUseIconActorName points to."""
    alias = str(a.get("itemUseIconActorName", "")) if a is not None else ""
    if alias and alias in by_name:
        return alias
    # Some have no icon alias and say it in their group instead (Item_Mushroom_D, the Rushroom of the climbing
    # armor's recipes, is in the group of Item_MushroomGet_D, the one the pouch holds; found on 26/11).
    group = str(a.get("systemSameGroupActorName", "")) if a is not None else ""
    return group if group and group != str(a.get("name", "")) and group in by_name else None


def level_lines(boot, names, lines):
    """The game's hidden level (Ecosystem/LevelSensor.byml, read by ksys::eco::LevelSensor): the kill-count flags with
    their points, the two factors that turn points into the enemy and weapon levels, and, for each enemy family, the
    level each of its ranks appears at."""
    sensor = oead.byml.from_binary(oead.yaz0.decompress(bytes(boot.get_file("Ecosystem/LevelSensor.sbyml").data)))
    setting = sensor["setting"]
    lines.append("LEVELSET %08x %08x" % (f32_bits(setting["Level2EnemyPower"]), f32_bits(setting["Level2WeaponPower"])))
    labelled = set()
    # The most each flag can hold (MaxValue in GameData/gamedata.ssarc; a kill counter stops at it).
    limits = {}
    gamedata = oead.Sarc(oead.yaz0.decompress(bytes(boot.get_file("GameData/gamedata.ssarc").data)))
    for f in gamedata.get_files():
        if "s32_data" in f.name or "bool_data" in f.name:
            for table in oead.byml.from_binary(bytes(f.data)).values():
                for entry in table:
                    if "DataName" in entry and "MaxValue" in entry:
                        value = entry["MaxValue"]
                        limits[str(entry["DataName"])] = 1 if isinstance(value, bool) else int(value)

    def label(actor):
        if actor in names and actor not in labelled:
            lines.append("LABEL %08x %s" % (crc(actor), names[actor]))
            labelled.add(actor)

    for f in sensor["flag"]:
        flag = str(f["name"])
        actor = flag[len("Defeated_"):-len("_Num")] if flag.startswith("Defeated_") and flag.endswith("_Num") else flag
        lines.append("LEVELFLAG %08x %s %08x %d" % (crc(flag), actor, f32_bits(f["point"]), limits.get(flag, 0)))
        label(actor)
    for s in sensor["enemy"]:
        actors = list(s["actors"])
        lines.append("LEVELSPECIES %s %d %s" % (str(s["species"]), len(actors), " ".join(
            "%s %08x" % (str(a["name"]), f32_bits(a["value"])) for a in actors)))
        for a in actors:
            label(str(a["name"]))


def reserve_lines(info, romfs, names, cook_actors, folder, lines):
    by_name = {str(a["name"]): a for a in info["Actors"]}
    sold, crafted = scan_shops(romfs)
    materials = set()

    def material(n):
        if not n.startswith("Armor_"):
            n = use_icon_name(by_name.get(n), by_name) or n
        return n

    def ingredients(a):
        return [(material(n), c) for n, c in recipe_of(a)]

    for a in info["Actors"]:
        name = str(a["name"])
        if not name.startswith("Armor_") or str(a.get("profile", "")) not in ("ArmorHead", "ArmorUpper", "ArmorLower"):
            continue
        nxt = str(a.get("armorNextRankName", ""))
        ingr = ingredients(a)
        materials.update(n for n, _ in ingr if not n.startswith("Armor_"))
        lines.append("ARMOR %08x %s %s %d %d %d %s %d %s" % (
            crc(name), name, "%08x" % crc(nxt) if nxt else "-", int(a.get("armorStarNum", 1)),
            int(a.get("itemBuyingPrice", 0)), int(a.get("itemCreatingPrice", 0)),
            str(a.get("seriesArmorSeriesType", "")) or "-", len(ingr),
            " ".join("%08x %d" % (crc(n), c) for n, c in ingr)))
        if name in names:
            lines.append("LABEL %08x %s" % (crc(name), names[name]))

    for name in sorted(crafted):
        a = by_name.get(name)
        if a is None or name.startswith("Armor_") or int(a.get("itemCreatingPrice", 0)) <= 0:
            continue
        ingr = ingredients(a)
        materials.update(n for n, _ in ingr)
        lines.append("CRAFT %08x %s %d %d %s" % (crc(name), name, int(a["itemCreatingPrice"]), len(ingr),
                                                 " ".join("%08x %d" % (crc(n), c) for n, c in ingr)))
        if name in names:
            lines.append("LABEL %08x %s" % (crc(name), names[name]))

    for name in sorted(sold):
        lines.append("SHOP %08x new" % crc(name))
    for name in sorted(crafted):
        lines.append("SHOP %08x craft" % crc(name))

    for name in sorted(materials - cook_actors):
        a = by_name.get(name, {})
        lines.append("ITEM %08x %s %d %d" % (crc(name), name, int(a.get("itemSellingPrice", 0)),
                                             int(a.get("itemBuyingPrice", 0))))
        if name in names:
            lines.append("LABEL %08x %s" % (crc(name), names[name]))

    # Side quests that take items away. The list is kept next to the output (it names quests: spoilers), as
    # tab-separated lines: finish flag, quest title, item actor, count ("?" when the game's text gives none).
    quests = folder / "quests-consume.tsv"
    if quests.exists():
        for row in quests.read_text(encoding="utf-8").splitlines():
            if not row.strip() or row.startswith("#"):
                continue
            flag, title, item, count = row.split("\t")
            lines.append("QUEST %08x %s %08x %s %s" % (crc(flag), flag, crc(item), count, title))


if __name__ == "__main__":
    main()
