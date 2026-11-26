#!/usr/bin/env python3
"""Makes quests.txt for BotW Unexplored: where each side and shrine quest is taken, with its name and description.

    python3 tools/gen_quests.py <romfs> [<dlc romfs>] > quests.txt

The data comes from the game's own files (a dump of version 1.6.0), so the output goes to the SD card only
(sdmc:/switch/botw-unexplored/quests.txt) and never into this repository. No dependencies: the formats are read by
tools/nxfmt.py.

- The quests: Pack/TitleBG.pack -> Quest/QuestProduct.sbquestpack. Type Mini is a side quest, Sub a shrine quest
  (main quests are left out: the story leads to them). The flags: <name>_Activated when the quest is taken (the
  NextFlag of its first step, Ready), the NextFlag of its last step when it is done, and QuestDependencyFlag when
  something has to happen before it can be taken.
- Where: the giver (Orderer) as placed in the map units of the world (the static ones in Pack/TitleBG.pack, the
  dynamic ones in Map/MainField; those of the DLC replace the base ones of the same area). A giver placed more than
  once: the placement nearest the quest's location. No giver on the map: the location's marker (objects whose
  MessageID is the location); else the first marker of its steps (IndicatorLocations).
- Names: Message/Msg_USen.product.ssarc in Pack/Bootup_USen.pack: QuestMsg/QL_<name>.msbt (<name>_Name and the
  first description) and StaticMsg/LocationMarker.msbt for the location.

Each line: kind (side or shrine), dlc (0/1), the CRC32 of the start, end and dependency flags (0 for none), x, z,
name, location, description; tab separated, after a first line "# BOTW_UNEXPLORED_QUESTS 1". A note on how each
position was found goes to stderr.
"""
import math
import pathlib
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import nxfmt  # noqa: E402

KINDS = {"Mini": "side", "Sub": "shrine"}


def crc(flag):
    return zlib.crc32(flag.encode("utf-8")) if flag else 0


def clean(text):
    return " ".join(text.replace("\t", " ").split())


def map_units(romfs, dlc):
    """The objects of every area of the world map: {area: [objects]}, the DLC's units replacing the base ones."""
    title = nxfmt.sarc((romfs / "Pack/TitleBG.pack").read_bytes())
    static = {n.split("/")[2]: title[n] for n in title if n.startswith("Map/MainField/") and n.endswith("_Static.smubin")}
    dynamic = {p.parent.name: p.read_bytes() for p in (romfs / "Map/MainField").glob("*/*_Dynamic.smubin")}
    if dlc:
        for p in (dlc / "Map/MainField").glob("*/*_Dynamic.smubin"):
            dynamic[p.parent.name] = p.read_bytes()
        for p in (dlc / "Map/MainField").glob("*/*_Static.smubin"):
            static[p.parent.name] = p.read_bytes()
        pack = dlc / "Pack/AocMainField.pack"
        if pack.exists():
            aoc = nxfmt.sarc(pack.read_bytes())
            for n in aoc:
                if n.startswith("Map/MainField/") and n.endswith("_Static.smubin"):
                    static[n.split("/")[2]] = aoc[n]
    areas = {}
    for source in (static, dynamic):
        for area, data in source.items():
            areas.setdefault(area, []).extend(nxfmt.byml(data).get("Objs", []))
    return areas


def main():
    romfs = pathlib.Path(sys.argv[1])
    dlc = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else None
    title = nxfmt.sarc((romfs / "Pack/TitleBG.pack").read_bytes())
    quests = nxfmt.byml(title["Quest/QuestProduct.sbquestpack"])
    boot = nxfmt.sarc((romfs / "Pack/Bootup_USen.pack").read_bytes())
    msg = nxfmt.sarc(boot["Message/Msg_USen.product.ssarc"])
    places = nxfmt.msbt_texts(msg["StaticMsg/LocationMarker.msbt"])

    placed = {}   # actor -> [(x, z)]
    markers = {}  # location key -> [(x, z)]
    for objs in map_units(romfs, dlc).values():
        for o in objs:
            x, _, z = o.get("Translate", [0.0, 0.0, 0.0])
            placed.setdefault(o.get("UnitConfigName", ""), []).append((x, z))
            message = (o.get("!Parameters") or {}).get("MessageID")
            if message:
                markers.setdefault(message, []).append((x, z))

    out = ["# BOTW_UNEXPLORED_QUESTS 1"]
    how = {"giver": 0, "giver nearest the location": 0, "location": 0, "step marker": 0, "none": 0}
    for q in quests:
        kind = KINDS.get(q.get("Type"))
        steps = q.get("Steps") or []
        if not kind or not steps or not (q.get("Orderer") or q.get("Location")):
            continue
        name = q["Name"]
        start = steps[0].get("NextFlag", "")
        finish = steps[-1].get("NextFlag", "")
        requires = q.get("QuestDependencyFlag") or ""
        location = q.get("Location") or ""
        spots = markers.get(location, [])
        givers = placed.get(q.get("Orderer") or "", [])
        if len(givers) == 1:
            where, way = givers[0], "giver"
        elif givers:
            ref = spots[0] if spots else givers[0]
            where = min(givers, key=lambda p: math.hypot(p[0] - ref[0], p[1] - ref[1]))
            way = "giver nearest the location"
        elif spots:
            where, way = spots[0], "location"
        else:
            where, way = None, "none"
            for step in steps:
                for marker in step.get("IndicatorLocations") or []:
                    where, way = (marker["Location"]["X"], marker["Location"]["Z"]), "step marker"
                    break
                if where:
                    break
        how[way] += 1
        if where is None:
            print("no position:", name, file=sys.stderr)
            continue
        key = "QuestMsg/QL_%s.msbt" % name
        texts = nxfmt.msbt_texts(msg[key]) if key in msg else {}
        title_text = texts.get("QL_%s_Name" % name, name)
        desc = texts.get("QL_%s_Desc" % name) or next((v for k, v in texts.items() if not k.endswith("_Name")), "")
        out.append("\t".join([kind, "1" if q.get("AocVersion") else "0", str(crc(start)), str(crc(finish)),
                              str(crc(requires)), "%.2f" % where[0], "%.2f" % where[1], clean(title_text),
                              clean(places.get(location, "")), clean(desc)]))
    sys.stdout.buffer.write(("\n".join(out) + "\n").encode("utf-8"))  # UTF-8 whatever the console uses
    print("positions:", how, file=sys.stderr)


if __name__ == "__main__":
    main()
