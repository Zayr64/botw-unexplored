#!/usr/bin/env python3
"""Generates source/ProgressData.cpp: the flags behind the counters of the progress panel.

    python3 tools/gen_progress.py --download work
    python3 tools/gen_progress.py work

The first command fetches into a folder (not part of this repository) what the second one reads:

  work/quests/*.quest.yml   the quests of the game, one file each, from https://github.com/leoetlino/botw
                            (Quest/, the game's QuestProduct.bquestpack in readable form)
  work/dungeon-chests.json  the chests inside the Divine Beasts and the final trial of the Champions' Ballad,
                            from the object map of zeldamods (https://objmap.zeldamods.org)
  work/hashes.csv           the names of the save flags, from https://github.com/marcrobledo/savegame-editors

Quests: every quest starts with a step called Ready whose flag (<name>_Activated) is set when the quest is taken,
and ends with a step whose flag is set when it is done. The type is Main, Sub (the shrine quests) or Mini (the
side quests); entries without a giver and without a location are pieces of minigames and are left out.
Chests: MainFieldDungeon_<actor>_<hash id>, set when opened. Compendium: IsRegisteredPictureBook_<actor>.
The app stores the CRC32 of each flag name.
"""
import json
import pathlib
import re
import sys
import time
import urllib.request
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
DIVINE_BEASTS = ['RemainsWind', 'RemainsElectric', 'RemainsFire', 'RemainsWater']
FINAL_TRIAL = 'FinalTrial'
KINDS = {'Main': 'Main', 'Sub': 'Shrine', 'Mini': 'Side'}


def crc(name):
    return zlib.crc32(name.encode()) & 0xFFFFFFFF


def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'gen_progress'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def download(work):
    quests = work / 'quests'
    quests.mkdir(parents=True, exist_ok=True)
    listing = json.loads(fetch('https://api.github.com/repos/leoetlino/botw/contents/Quest?ref=master'))
    for entry in listing:
        name = entry['name']
        if name.endswith('.quest.yml') and '/' not in name and '\\' not in name:
            (quests / name).write_bytes(fetch(entry['download_url']))
            time.sleep(0.05)
    chests = {}
    for name in DIVINE_BEASTS + [FINAL_TRIAL]:
        url = f'https://radar.zeldamods.org/objs/MainFieldDungeon/{name}?q=actor%3A%5E%22TBox_%22&limit=200'
        chests[name] = json.loads(fetch(url))
        time.sleep(0.15)
    (work / 'dungeon-chests.json').write_text(json.dumps(chests), encoding='utf-8')
    (work / 'hashes.csv').write_bytes(fetch(
        'https://raw.githubusercontent.com/marcrobledo/savegame-editors/master/zelda-botw/zelda-botw.hashes.csv'))
    print(f'{len(list(quests.glob("*.quest.yml")))} quests, {sum(len(c) for c in chests.values())} chests, hash list')


def read_quest(path):
    """Top-level values and, for each step, its name and flag. No YAML library: the files are regular."""
    quest = {'steps': []}
    in_steps = False
    step = None
    for line in path.read_text(encoding='utf-8').splitlines():
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
    return quest


def generate(work):
    quests = []
    for path in sorted((work / 'quests').glob('*.quest.yml')):
        quest = read_quest(path)
        if not quest.get('Orderer') and not quest.get('Location'):
            continue
        steps = quest['steps']
        assert steps and steps[0].get('Name') == 'Ready', path.name
        start, finish = steps[0]['NextFlag'], steps[-1]['NextFlag']
        assert start == quest['Name'] + '_Activated' and start != finish, path.name
        quests.append((KINDS[quest['Type']], 1 if quest.get('AocVersion') else 0, crc(start), crc(finish), quest['Name']))
    quests.sort(key=lambda q: (list(KINDS.values()).index(q[0]), q[1], q[2]))
    assert len({q[2] for q in quests} | {q[3] for q in quests}) == 2 * len(quests), 'a quest flag is used twice'

    chests = json.loads((work / 'dungeon-chests.json').read_text(encoding='utf-8'))

    def chest_flags(maps):
        return sorted(crc(f"MainFieldDungeon_{chest['name']}_{chest['hash_id']}") for name in maps for chest in chests[name])

    compendium = []
    for line in (work / 'hashes.csv').read_text(encoding='utf-8', errors='replace').splitlines():
        parts = line.split(';')
        if len(parts) >= 3 and parts[2].startswith('IsRegisteredPictureBook_'):
            assert int(parts[0]) == crc(parts[2]), parts[2]
            compendium.append(crc(parts[2]))
    compendium.sort()

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
        f'{len(chest_flags([FINAL_TRIAL]))} in the final trial, {len(compendium)} compendium flags.',
        '#include "ProgressData.h"',
        '',
        'namespace Data',
        '{',
        'const Quest Quests[] = {',
    ]
    for kind, dlc, start, finish, _ in quests:
        out.append(f'    {{{start}u, {finish}u, static_cast<uint8_t>(QuestKind::{kind}), {dlc}}},')
    out.append('};')
    out.append('const int QuestsCount = sizeof(Quests) / sizeof(Quests[0]);')
    out.append('')
    out.extend(flag_array('DivineBeastChestFlags', chest_flags(DIVINE_BEASTS)))
    out.extend(flag_array('FinalTrialChestFlags', chest_flags([FINAL_TRIAL])))
    out.extend(flag_array('CompendiumFlags', compendium))
    out[-1] = '}'
    (ROOT / 'source' / 'ProgressData.cpp').write_text('\n'.join(out) + '\n', encoding='utf-8', newline='\n')

    counts = {}
    for kind, dlc, _, _, _ in quests:
        counts[(kind, dlc)] = counts.get((kind, dlc), 0) + 1
    print('quests:', ', '.join(f"{kind}{' (DLC)' if dlc else ''} {n}" for (kind, dlc), n in sorted(counts.items())))
    print(f'chests: Divine Beasts {len(chest_flags(DIVINE_BEASTS))}, final trial {len(chest_flags([FINAL_TRIAL]))}; '
          f'compendium flags: {len(compendium)}')


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--download':
        download(pathlib.Path(sys.argv[2]))
    elif len(sys.argv) == 2:
        generate(pathlib.Path(sys.argv[1]))
    else:
        raise SystemExit(__doc__)
