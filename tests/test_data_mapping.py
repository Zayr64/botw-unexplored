#!/usr/bin/env python3
"""Lightweight checks that keep data counts and completion hashes consistent."""

import pathlib
import re
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = (ROOT / "source" / "Data.h").read_text(encoding="utf-8")
DATA = (ROOT / "source" / "Data.cpp").read_text(encoding="utf-8")


def count_entries(type_name: str, array_name: str) -> list[str]:
    match = re.search(
        rf"Data::{type_name} {array_name}\[[^]]+\] = \{{(.*?\)\s*\}};)",
        DATA,
        re.DOTALL,
    )
    if not match:
        raise AssertionError(f"{type_name} data array not found")
    return re.findall(rf"Data::{type_name}\((\d+),", match.group(1))


def declared_count(name: str) -> int:
    match = re.search(rf"const int {name}Count = (\d+);", HEADER)
    if not match:
        raise AssertionError(f"{name} count not declared")
    return int(match.group(1))


for plural, type_name, array_name in (
    ("Koroks", "Korok", "Koroks"),
    ("Shrine", "Shrine", "Shrines"),
    ("DLCShrine", "DLCShrine", "DLCShrines"),
    ("Locations", "Location", "Locations"),
    ("Hinoxes", "Hinox", "Hinoxes"),
    ("Taluses", "Talus", "Taluses"),
    ("Moldugas", "Molduga", "Moldugas"),
):
    hashes = count_entries(type_name, array_name)
    assert len(hashes) == declared_count(plural), (type_name, len(hashes), declared_count(plural))
    assert len(hashes) == len(set(hashes)), f"duplicate {type_name} completion hash"

# Location flags are named Location_<internal name>; the stored hash is its CRC32.
# These entries were paired with a neighbouring room/stable flag before; the
# internal names follow MrCheeze/botw-waypoint-map, the source of the names and
# positions in Data.cpp.
EXPECTED_LOCATION_FLAGS = {
    "East Passage": "Location_HyruleCastle_Room_10",
    "West Passage": "Location_HyruleCastle_Room_11",
    "Dining Hall": "Location_HyruleCastle_Room_1",
    "South Akkala Stable": "Location_TamourHatago2",
    "East Akkala Stable": "Location_TamourHatago",
    "Hyrule Castle": "Location_HyruleCastle",
}
for display_name, flag in EXPECTED_LOCATION_FLAGS.items():
    match = re.search(rf'Data::Location\((\d+), "{re.escape(display_name)}"', DATA)
    assert match, display_name
    assert int(match.group(1)) == zlib.crc32(flag.encode()), (display_name, flag)

# Korok guide ids must be unique except for the fairy-light pair that shares one guide.
guide_ids = re.findall(r"Data::Korok\(\d+, \"[^\"]+\", [^,]+, [^,]+, (\d+)\)", DATA)
assert len(guide_ids) == declared_count("Koroks"), len(guide_ids)
duplicates = {i for i in guide_ids if guide_ids.count(i) > 1}
assert duplicates == {"78"}, duplicates
