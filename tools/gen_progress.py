#!/usr/bin/env python3
"""Generates source/ProgressData.cpp: the flags behind the counters of the progress and collection panels.

    python3 tools/gen_progress.py --download work
    python3 tools/gen_progress.py work

The first command fetches into a folder (not part of this repository) what the second one reads:

  work/botw/                 part of https://github.com/leoetlino/botw, the game's data in readable form (needs git):
                             Quest/ (the quests, one file each), the armor actors of Actor/ActorLink and
                             Actor/GeneralParamList, and the amiibo drop tables of Actor/DropTable
  work/dungeon-chests.json   the chests inside the Divine Beasts and the final trial of the Champions' Ballad,
  work/world-chests.json     the chests of the world map,
  work/npcs.json             where the characters stand,
  work/MapTower.json         the outlines of the fifteen regions (see gen_regions.py) and
  work/names.json            the English names of the actors, from the object map of zeldamods
                             (https://objmap.zeldamods.org)
  work/hashes.csv            the names of the save flags, from https://github.com/marcrobledo/savegame-editors
  work/compendium.json       the entries of the Hyrule Compendium with their category, and
  work/compendium-master.json  those that only Master Mode has, from https://api.hyrule-compendium.com

Quests: every quest starts with a step called Ready whose flag (<name>_Activated) is set when the quest is taken,
and ends with a step whose flag is set when it is done. The type is Main, Sub (the shrine quests) or Mini (the
side quests); entries without a giver and without a location are pieces of minigames and are left out. Some have
a flag that must be set before they can be taken (QuestDependencyFlag). For the shrine and side quests of the
base game the region where the quest is taken is worked out here: that of its giver, when the giver stands in
one region only; else that of its location; else that of the first marker of its steps.

Chests: MainFieldDungeon_<actor>_<hash id>, set when opened.

Compendium: IsRegisteredPictureBook_<actor>, set when the entry has a picture. The game data does not say which
category an entry is in, so the flags are matched by name with the public list of entries. Four entries come
with the Champions' Ballad and five more exist in Master Mode only.

Armor: IsGet_<actor>, set when the piece is first obtained. Each level of an upgraded piece is an actor of its
own (NextRankName in its parameters), so a piece is a chain of flags and is owned when any of them is set. The
pieces that only amiibo give are left out; those found only in chests of the Expansion Pass are marked.

Memories: IsPlayed_Demo126_0 to IsPlayed_Demo138_0, the scenes the event of a memory spot plays (RememberTag in
the game's event flows); IsGet_MemoryPhoto_NNN are the pictures in the album.

The app stores the CRC32 of each flag name.
"""
import collections
import json
import pathlib
import re
import subprocess
import sys
import time
import urllib.request
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_regions import in_polygon  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[1]
DIVINE_BEASTS = ['RemainsWind', 'RemainsElectric', 'RemainsFire', 'RemainsWater']
FINAL_TRIAL = 'FinalTrial'
KINDS = {'Main': 'Main', 'Sub': 'Shrine', 'Mini': 'Side'}
REGIONS = 15

# The order of the categories in the game's compendium, and how many entries each has (with the Champions' Ballad).
CATEGORIES = [('creatures', 'Creatures', 83), ('monsters', 'Monsters', 81), ('materials', 'Materials', 36),
              ('equipment', 'Equipment', 185), ('treasure', 'Treasure', 4)]
# The four entries the Champions' Ballad adds to the 385 of the base game.
COMPENDIUM_DLC = {'Priest_Boss_Normal', 'Enemy_Golem_Fire_R', 'Enemy_SandwormR', 'Weapon_Sword_502'}
# The object map and the list of entries name this one differently.
COMPENDIUM_ALIASES = {'luminous stone deposit': 'luminous ore deposit'}

MEMORY_SCENES = range(126, 139)
# Two actors are the same piece: the game has a second pair of the same boots, which cannot be upgraded.
ARMOR_SAME_PIECE = {'Armor_140_Lower': 'Armor_141_Lower'}
ARMOR_PIECES = {'base': 65, 'dlc': 19, 'amiibo': 23}


def crc(name):
    return zlib.crc32(name.encode()) & 0xFFFFFFFF


def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'gen_progress'})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def download(work):
    work.mkdir(parents=True, exist_ok=True)
    botw = work / 'botw'
    if not (botw / '.git').exists():
        subprocess.run(['git', 'clone', '--quiet', '--depth', '1', '--filter=blob:none', '--no-checkout',
                        'https://github.com/leoetlino/botw.git', str(botw)], check=True)
    subprocess.run(['git', '-C', str(botw), 'sparse-checkout', 'set', '--no-cone', '/Quest/',
                    '/Actor/ActorLink/Armor_*', '/Actor/GeneralParamList/Armor_*',
                    '/Actor/DropTable/Item_Amiibo_*'], check=True)
    subprocess.run(['git', '-C', str(botw), 'checkout', '--quiet', 'HEAD'], check=True)

    chests = {}
    for name in DIVINE_BEASTS + [FINAL_TRIAL]:
        url = f'https://radar.zeldamods.org/objs/MainFieldDungeon/{name}?q=actor%3A%5E%22TBox_%22&limit=200'
        chests[name] = json.loads(fetch(url))
        time.sleep(0.15)
    (work / 'dungeon-chests.json').write_text(json.dumps(chests), encoding='utf-8')
    for name, url in (
        ('world-chests.json', 'https://radar.zeldamods.org/objs/MainField/?q=actor%3A%5E%22TBox_%22&limit=10000'),
        ('npcs.json', 'https://radar.zeldamods.org/objs/MainField/?q=actor%3A%5E%22Npc_%22&limit=10000'),
        ('MapTower.json', 'https://objmap.zeldamods.org/game_files/ecosystem/MapTower.json'),
        ('names.json', 'https://objmap.zeldamods.org/game_files/names.json'),
        ('hashes.csv', 'https://raw.githubusercontent.com/marcrobledo/savegame-editors/master/zelda-botw/zelda-botw.hashes.csv'),
        ('compendium.json', 'https://api.hyrule-compendium.com/v3/compendium/all?game=botw'),
        ('compendium-master.json', 'https://api.hyrule-compendium.com/v3/compendium/master_mode/all'),
    ):
        (work / name).write_bytes(fetch(url))
        time.sleep(0.15)
    print(f'{len(list((botw / "Quest").glob("*.quest.yml")))} quests, '
          f'{len(list((botw / "Actor" / "ActorLink").glob("Armor_*.yml")))} armor actors, '
          f'{sum(len(c) for c in chests.values())} dungeon chests and the lists')


def read_quest(path):
    """Top-level values and, for each step, its name and flag. No YAML library: the files are regular."""
    quest = {'steps': [], 'markers': []}
    in_steps = False
    step = None
    text = path.read_text(encoding='utf-8')
    for line in text.splitlines():
        top = re.match(r'^(\w+): ?(.*)$', line)
        if top:
            quest[top.group(1)] = top.group(2).strip().strip("'")
            in_steps = top.group(1) == 'Steps'
            step = None
            continue
        if not in_steps:
            continue
        first = re.match(r'^- (\w+): ?(.*)$', line)
        if first:
            step = {}
            quest['steps'].append(step)
            step[first.group(1)] = first.group(2).strip().strip("'")
            continue
        key = re.match(r'^  (\w+): ?(.*)$', line)
        if key and step is not None:
            step[key.group(1)] = key.group(2).strip().strip("'")
    # Where the steps point to on the map, in the order of the file: (x, z)
    steps_text = text.split('\nSteps:', 1)[1] if '\nSteps:' in text else ''
    quest['markers'] = [(float(x), float(z)) for x, z in
                        re.findall(r'X: (-?[\d.]+)\n\s+Y: -?[\d.]+\n\s+Z: (-?[\d.]+)', steps_text)]
    return quest


def flag_names(work):
    """The names of the list whose hash is the CRC32 of the name (a few placeholders are not)."""
    names = set()
    for line in (work / 'hashes.csv').read_text(encoding='utf-8', errors='replace').splitlines():
        parts = line.split(';')
        if len(parts) >= 3 and parts[0].isdigit() and int(parts[0]) == crc(parts[2]):
            names.add(parts[2])
    return names


def read_quests(work):
    outlines = json.loads((work / 'MapTower.json').read_text(encoding='utf-8'))

    def region_of(x, z):
        found = [int(k) for k, polygons in outlines.items() if any(in_polygon(x, z, p) for p in polygons)]
        return found[0] if len(found) == 1 else None

    givers = collections.defaultdict(list)
    for npc in json.loads((work / 'npcs.json').read_text(encoding='utf-8')):
        givers[npc['name']].append((npc['pos'][0], npc['pos'][2]))
    data = (ROOT / 'source' / 'Data.cpp').read_text(encoding='utf-8')
    places = {}  # hash of Location_<name> -> (x, z): the locations and the shrines of the app
    for m in re.finditer(r'Data::(?:Location|Shrine|DLCShrine)\((\d+),(?: "[^"]*",)? (-?[\d.]+)f?, (-?[\d.]+)f?', data):
        places[int(m.group(1))] = (float(m.group(2)), float(m.group(3)))

    def taken_in(quest):
        spots = {region_of(x, z) for x, z in givers.get(quest.get('Orderer') or '', [])}
        if len(spots) == 1 and None not in spots:
            return spots.pop()
        place = places.get(crc('Location_' + (quest.get('Location') or '')))
        if place and region_of(*place) is not None:
            return region_of(*place)
        if quest['markers'] and region_of(*quest['markers'][0]) is not None:
            return region_of(*quest['markers'][0])
        return -1

    quests = []
    for path in sorted((work / 'botw' / 'Quest').glob('*.quest.yml')):
        quest = read_quest(path)
        if not quest.get('Orderer') and not quest.get('Location'):
            continue
        steps = quest['steps']
        assert steps and steps[0].get('Name') == 'Ready', path.name
        start, finish = steps[0]['NextFlag'], steps[-1]['NextFlag']
        assert start == quest['Name'] + '_Activated' and start != finish, path.name
        requires = quest.get('QuestDependencyFlag') or ''
        assert not requires or quest.get('QuestDependencyFlagType') == 'bool', path.name
        kind = KINDS[quest['Type']]
        dlc = 1 if quest.get('AocVersion') else 0
        region = taken_in(quest) if kind in ('Shrine', 'Side') and not dlc else -1
        quests.append({'kind': kind, 'dlc': dlc, 'start': crc(start), 'finish': crc(finish),
                       'requires': crc(requires) if requires else 0, 'region': region})
    quests.sort(key=lambda q: (list(KINDS.values()).index(q['kind']), q['dlc'], q['start']))
    assert len({q['start'] for q in quests} | {q['finish'] for q in quests}) == 2 * len(quests), 'a quest flag is used twice'
    assert all(-1 <= q['region'] < REGIONS for q in quests)
    return quests


def read_compendium(work, names):
    display = json.loads((work / 'names.json').read_text(encoding='utf-8'))

    def key(name):
        name = name.lower().replace('’', '').replace("'", '')
        name = re.sub(r'[^a-z0-9+]+', ' ', name).strip()
        return COMPENDIUM_ALIASES.get(name, name)

    def by_name(file):
        table = {}
        for entry in json.loads((work / file).read_text(encoding='utf-8'))['data']:
            assert key(entry['name']) not in table, entry['name']
            table[key(entry['name'])] = entry['category']
        return table

    normal, master = by_name('compendium.json'), by_name('compendium-master.json')
    order = [c[0] for c in CATEGORIES]
    entries = []
    for name in sorted(names):
        if not name.startswith('IsRegisteredPictureBook_'):
            continue
        actor = name[len('IsRegisteredPictureBook_'):]
        entry = key(display[actor])
        if entry in normal:
            entries.append((crc(name), order.index(normal[entry]), 'CompendiumDlc' if actor in COMPENDIUM_DLC else '0'))
        else:
            entries.append((crc(name), order.index(master[entry]), 'CompendiumMasterMode'))
    entries.sort()
    assert len({e[0] for e in entries}) == len(entries)
    for index, (_, _, total) in enumerate(CATEGORIES):
        assert sum(1 for e in entries if e[1] == index and e[2] != 'CompendiumMasterMode') == total, CATEGORIES[index]
    assert sum(1 for e in entries if e[2] == 'CompendiumDlc') == len(COMPENDIUM_DLC)
    assert sum(1 for e in entries if e[2] == 'CompendiumMasterMode') == 5
    return entries


def read_armor(work, names):
    """The pieces that can be had without amiibo: (flags of the levels, dlc), in the order of the actors."""
    botw = work / 'botw' / 'Actor'
    actors = {}  # actor -> (series, next level)
    for path in sorted((botw / 'ActorLink').glob('Armor_*.yml')):
        user = re.search(r'^\s*GParamUser: (\S+)', path.read_text(encoding='utf-8'), re.M).group(1)
        if not user.startswith('Armor_'):
            continue  # a model for a cutscene or a display, not something to wear
        text = (botw / 'GeneralParamList' / (user + '.gparamlist.yml')).read_text(encoding='utf-8')
        series = re.search(r'SeriesType: !str32 ?(\S*?)[,}\s]', text)
        following = re.search(r'NextRankName: !str64 ?(\S*)', text)
        actors[path.name[:-len('.yml')]] = (series.group(1) if series else '', following.group(1) if following else '')
    upgrades = {following for _, following in actors.values() if following}

    amiibo = set()
    for path in (botw / 'DropTable').glob('Item_Amiibo_*.drop.yml'):
        amiibo |= set(re.findall(r'Armor_\d+_\w+', path.read_text(encoding='utf-8')))
    chests = collections.defaultdict(list)  # armor actor -> the chests of the world map that hold it
    for chest in json.loads((work / 'world-chests.json').read_text(encoding='utf-8')):
        drop = chest.get('drop')
        if isinstance(drop, list) and len(drop) > 1 and isinstance(drop[1], str) and 'Armor_' in drop[1]:
            chests[drop[1].replace('Obj_', '')].append(chest['name'])

    pieces = collections.OrderedDict()  # first actor of the piece -> [flags, kind]
    counts = collections.Counter()
    for actor in sorted(actors):
        series = actors[actor][0]
        # Without a flag of its own: a variant of another actor for a cutscene or for a character
        if actor in upgrades or series == 'Naked' or 'IsGet_' + actor not in names:
            continue
        levels = [actor]
        while actors[levels[-1]][1] in actors:
            levels.append(actors[levels[-1]][1])
        flags = [crc('IsGet_' + level) for level in levels if 'IsGet_' + level in names]
        if actor in amiibo:
            counts['amiibo'] += 1
            continue
        kind = 'dlc' if chests.get(actor) and all('Aoc' in name for name in chests[actor]) else 'base'
        piece = ARMOR_SAME_PIECE.get(actor, actor)
        if piece in pieces:
            pieces[piece][0].extend(flags)
        else:
            pieces[piece] = [flags, kind]
    for target in ARMOR_SAME_PIECE.values():
        assert target in pieces, target
    counts.update(kind for _, kind in pieces.values())
    assert dict(counts) == ARMOR_PIECES, dict(counts)
    return list(pieces.values())


def generate(work):
    names = flag_names(work)
    quests = read_quests(work)
    compendium = read_compendium(work, names)
    armor = read_armor(work, names)
    assert len(armor) < 256
    chests = json.loads((work / 'dungeon-chests.json').read_text(encoding='utf-8'))

    def chest_flags(maps):
        return sorted(crc(f"MainFieldDungeon_{chest['name']}_{chest['hash_id']}") for name in maps for chest in chests[name])

    memories = [f'IsPlayed_Demo{scene}_0' for scene in MEMORY_SCENES]
    photos = sorted(name for name in names if re.match(r'^IsGet_MemoryPhoto_\d+$', name))
    assert all(name in names for name in memories) and len(photos) == 14

    def flag_array(name, flags):
        lines = [f'const uint32_t {name}[] = {{']
        for i in range(0, len(flags), 8):
            lines.append('    ' + ' '.join(f'{flag}u,' for flag in flags[i:i + 8]))
        lines.append('};')
        lines.append(f'const int {name}Count = sizeof({name}) / sizeof({name}[0]);')
        lines.append('')
        return lines

    out = [
        '// Generated by tools/gen_progress.py. Do not edit.',
        f'// {len(quests)} quests, {len(chest_flags(DIVINE_BEASTS))} chests in the Divine Beasts, '
        f'{len(chest_flags([FINAL_TRIAL]))} in the final trial, {len(compendium)} compendium flags,',
        f'// {len(armor)} armor pieces, {len(memories)} memories.',
        '#include "ProgressData.h"',
        '',
        'namespace Data',
        '{',
        '// taken, done, needed before it can be taken, kind, Expansion Pass, region where it is taken',
        'const Quest Quests[] = {',
    ]
    for quest in quests:
        out.append(f"    {{{quest['start']}u, {quest['finish']}u, {quest['requires']}u, "
                   f"static_cast<uint8_t>(QuestKind::{quest['kind']}), {quest['dlc']}, {quest['region']}}},")
    out.append('};')
    out.append('const int QuestsCount = sizeof(Quests) / sizeof(Quests[0]);')
    out.append('')
    out.extend(flag_array('DivineBeastChestFlags', chest_flags(DIVINE_BEASTS)))
    out.extend(flag_array('FinalTrialChestFlags', chest_flags([FINAL_TRIAL])))

    out.append('const CompendiumEntry CompendiumEntries[] = {')
    for i in range(0, len(compendium), 4):
        out.append('    ' + ' '.join(
            f'{{{flag}u, static_cast<uint8_t>(CompendiumCategory::{CATEGORIES[category][1]}), {extra}}},'
            for flag, category, extra in compendium[i:i + 4]))
    out.append('};')
    out.append('const int CompendiumEntriesCount = sizeof(CompendiumEntries) / sizeof(CompendiumEntries[0]);')
    out.append('')

    out.append('// flag of one level of a piece, the piece, Expansion Pass')
    out.append('const ArmorFlag ArmorFlags[] = {')
    rows = [f'{{{flag}u, {piece}, {1 if kind == "dlc" else 0}}},'
            for piece, (flags, kind) in enumerate(armor) for flag in flags]
    for i in range(0, len(rows), 6):
        out.append('    ' + ' '.join(rows[i:i + 6]))
    out.append('};')
    out.append('const int ArmorFlagsCount = sizeof(ArmorFlags) / sizeof(ArmorFlags[0]);')
    out.append(f'const int ArmorPiecesCount = {len(armor)};')
    out.append('')

    out.extend(flag_array('MemoryFlags', [crc(name) for name in memories]))
    out.extend(flag_array('MemoryPhotoFlags', [crc(name) for name in photos]))
    out[-1] = '}'
    (ROOT / 'source' / 'ProgressData.cpp').write_text('\n'.join(out) + '\n', encoding='utf-8', newline='\n')

    counts = collections.Counter((q['kind'], q['dlc']) for q in quests)
    print('quests:', ', '.join(f"{kind}{' (DLC)' if dlc else ''} {n}" for (kind, dlc), n in sorted(counts.items())))
    regional = [q for q in quests if q['kind'] in ('Shrine', 'Side') and not q['dlc']]
    print(f"   taken in a known region: {sum(1 for q in regional if q['region'] >= 0)} of {len(regional)}; "
          f"with a flag needed first: {sum(1 for q in quests if q['requires'])}")
    print(f'chests: Divine Beasts {len(chest_flags(DIVINE_BEASTS))}, final trial {len(chest_flags([FINAL_TRIAL]))}')
    print('compendium:', ', '.join(f'{title} {total}' for _, title, total in CATEGORIES),
          f'= {sum(c[2] for c in CATEGORIES)}; {len(compendium)} flags')
    print(f"armor: {len(armor)} pieces ({sum(1 for _, kind in armor if kind == 'dlc')} of the Expansion Pass), "
          f'{sum(len(flags) for flags, _ in armor)} flags; memories: {len(memories)}, pictures: {len(photos)}')


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--download':
        download(pathlib.Path(sys.argv[2]))
    elif len(sys.argv) == 2:
        generate(pathlib.Path(sys.argv[1]))
    else:
        raise SystemExit(__doc__)
