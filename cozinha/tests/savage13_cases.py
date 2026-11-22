#!/usr/bin/env python3
"""Turns savage13/cooking's test dishes (t/*.json, results read from the game's screen) into cases for
test_cook.cpp: one line per dish, tab-separated, with actor names instead of English names.

Usage: python savage13_cases.py <cook-data.txt> <savage13 t folder> <output .tsv>
savage13/cooking: https://github.com/savage13/cooking (BSD-2-Clause). Its files are not copied here.
"""
import glob
import json
import os
import sys


def main():
    data_path, folder, out_path = sys.argv[1:4]
    by_display = {}
    tags_of = {}
    current = None
    for line in open(data_path, encoding="utf-8"):
        p = line.rstrip("\n").split(" ")
        if p[0] == "ACTOR":
            current = p[2]
            tags_of[current] = int(p[13])
        elif p[0] == "NAME":
            by_display.setdefault(" ".join(p[2:]), []).append(current)

    def actor_for(display):
        options = by_display.get(display, [])
        # Several actors share a name (e.g. a recipe alias and the item in the pouch): take the pouch item, the one
        # that is not an Item_Cook_* dish and has the most tags.
        options = [o for o in options if not o.startswith("Item_Cook_")] or options
        options.sort(key=lambda o: (-tags_of.get(o, 0), o))
        return options[0] if options else None

    rows, skipped = [], 0
    for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
        for i, case in enumerate(json.load(open(path, encoding="utf-8"))):
            actors = [actor_for(n) for n in case["ingredients"]]
            if None in actors:
                skipped += 1
                print("skipped (unknown ingredient):", case["ingredients"], file=sys.stderr)
                continue
            t = case.get("time")
            seconds = str(int(t.split(":")[0]) * 60 + int(t.split(":")[1])) if t else "-"

            def opt(key):
                v = case.get(key)
                return "-" if v is None else str(v)

            rows.append("\t".join([
                f"{os.path.basename(path)}#{i}", case["name"], ",".join(actors), case.get("effect") or "-",
                opt("hp"), opt("potency"), opt("level"), opt("stamina"), opt("stamina_extra"), seconds,
                opt("price")]))
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(rows) + "\n")
    print(f"{out_path}: {len(rows)} cases, {skipped} skipped")


if __name__ == "__main__":
    main()
