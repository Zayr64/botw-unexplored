# BotW Unexplored

BotW Unexplored is a Nintendo Switch homebrew map companion for *The Legend of Zelda: Breath of the Wild*. It reads the selected local save and shows collectible progress directly on the console. Opened from the Album while BotW 1.6.0 is running, it reads the progress (and where Link is) straight from the game's memory, read only, through Atmosphere's cheat service; a save backup made earlier is the fallback.

This repository is the maintained fork of [lud99/botw-unexplored](https://github.com/lud99/botw-unexplored). The current fork release is **v2.2.11**.

## Features

- Koroks, shrines, DLC shrines, Hinoxes, Taluses, and Moldugas.
- **Towers and Great Fairies**: the towers not activated yet and the fountains whose Great Fairy still sleeps, each in a category on by default, with the place's name and its state on the card.
- **Locations**: the named places of the map not visited yet (the third part of the map completion, with the Korok seeds and the shrines), as magenta pins, in a category on by default. The card tells the place's name; the names are not drawn on the map.
- **Breakable walls**: the 140 walls of the world map that a bomb opens, as one more category that starts switched off. The card only says whether the wall was broken.
- **Quests**: where each side and shrine quest not taken yet is given (where its giver stands), as an orange speech bubble; those that wait for something else to happen first show in grey. The card tells the kind of quest, its name and place, whether it can be taken now and the game's own description. A quest in progress (main ones too) shows as a green speech bubble with an ellipsis where its current step leads, as the game's own marker for that step does (each marker of the step, up to four, until each is turned off), or where it was taken when the step has no marker; its card has the quest log's text for that step. The legend counts the side and shrine quests done. The data is `sdmc:/switch/botw-unexplored/quests.txt`, made from the game's files (see "Quests on the map"); without it there are no quests on the map.
- **Treasure chests** of the world map that are still closed and hold something rare, in three categories that start switched off, each drawn as the symbol of its tab in the game's inventory instead of a chest (a purple tunic, a red sword, a gold gem): `Armor & key items` (armor, key items and horse gear: what is kept for good; a chest whose item the player already has, bought or given elsewhere, counts as done and leaves the map, from the IsGet flags of every level of the item made by `tools/gen_chest_owned.py`), `Rare weapons` (weapons, bows and shields found in at most two chests of the whole map) and `Rare materials` (the same for materials). A chest card shows what the chest holds (the item and how many) under the name of its group, whether it is opened and how far it is. The other chests, with arrows, gems, rupees and gear found in many chests, are not on the map: the Progress tab counts them. Chests of the Expansion Pass show once the save has a sign of it (one of them opened, the Champions' Ballad or a Master Mode save); chests that only exist in Master Mode show only there. Chests inside shrines and Divine Beasts are not on the map (a shrine card tells when one is left).
- **Hero's Path**: where Link has walked, from the save's `tracker/trackblockNN.sav` files, as a mint line over the
  map and under the icons, one width on the screen at every zoom; split where Link warped, died or went into a shrine.
  Its legend row (on by default) tells how far that is. The game keeps the newest part in memory, where the app does
  not read it: with the game open the line is the one of the last time the app ran with it closed (a copy of the files
  is kept in `saves/<profile>/tracker/`).
- **Live progress while playing**: with BotW 1.6.0 running behind the app (opened from the Album), the game's own flags are read from its memory every time the app opens, and again with `-`. Nothing is written to the game. The chip at the top right tells where the progress comes from: `Live from the game`, `Save backup` (the memory could not be read; the reason shows for a few seconds and goes to `log.txt`) or `Save`.
- Normal Mode and Master Mode, including save-backup support when BotW is running.
- A bar at the bottom shows what each button does in the current screen, and the name of the object under the aim.
- Object cards: type, name, what is done and what is left (a hollow dot), distance and direction from Link, game coordinates. Korok cards add the guide's photo (`A` enlarges it) and the hint.
- The legend, in two columns of seven (an icon and count over the category's name, and a toggle as in the Switch's System Settings: the selected cell in a cyan frame, as on the HOME menu), shows the map completion (computed like the game: Korok seeds, discovered shrines and locations over 1207) and a progress bar per category. The locations visited are also a number in the Progress tab.
- The left panel has four tabs, all in numbers only:
  - **Progress** counts what the map does not show: quests of each kind done and taken, towers, shrines completed and their chests, great fairies, Divine Beasts and their chests, the common chests of the world map, memories recalled (out of the pictures the album has so far), locations visited, and when the next Blood Moon comes, in minutes of play (from the world's clock and the Blood Moon timer, by the rule of the game's `TimeMgr` in the zeldaret/botw decomp).
  - **Regions** lists the fifteen regions of the map with what is found of each (Korok seeds and shrines) and shows one alone on the map: only its objects are drawn, counted in the legend and picked by `ZL`/`ZR`/`R`. Each row also tells how many shrine and side quests are taken in that region and are not in the log yet: `3 quests` can be taken now; with none of those, `2 later` wait for something else to happen first. A region is named once its tower is activated; until then it is an "Unknown region".
  - **Collection** has the Hyrule Compendium, in all and by category (385 entries, 389 with the Champions' Ballad, 5 more in Master Mode), and the armor pieces owned at any level of upgrade (65, or 84 with the Expansion Pass; those that only amiibo give are not counted).
- The map shows only what is still missing. Icons shrink on the overview, where they pile up; zoomed in they grow
  with the map up to zoom 3 and keep that size from there, so that they do not hide the place they mark.
- While the legend or a card is open the map slides to the right, so that the selected object stays in view, inside a ring.
- Per-profile and per-mode manual progress (`Y`) for every supported object type, only while a save backup is shown. It never modifies the BotW save.
- `ZR` / `ZL` step forward / back through the 5 nearest missing visible objects, opening the Korok hint or the card of each one.
- "You are here": a marker where Link is (right now when read from the game, otherwise the last save); the map opens there and `L` glides back to it. Inside a shrine the marker is on the shrine.
- Shrine cards show whether the shrine is completed and how many of its chests are opened (`Chests 2/3`); a discovered shrine with something left stays on the map.
- The shrines of the Champions' Ballad show only once that quest has begun, when they can exist in the world. Before that the legend counts 120 shrines.
- The most recent save slot is picked by the save time in `caption.sav`, so loading an older save is followed.
- Optional high-resolution map (4x, from the Album too with its BC1 chunks; with JPEG chunks only, 2x from the Album and 4x zoomed in while memory allows) when zoomed in, read from the SD card (see below).
- Persisted camera, zoom, and legend visibility. Legacy settings are migrated safely.

## Installation

1. Download `botw-unexplored.nro` from [Releases](https://github.com/sklart/botw-unexplored/releases).
2. Copy it to `sdmc:/switch/botw-unexplored/botw-unexplored.nro`.
3. Start it from the Homebrew Menu and select the Switch profile that owns the BotW save.

Start BotW Unexplored once with the game closed: it keeps a backup of the selected save, shown when the memory of the running game cannot be read (another game version, for instance).

## Controls

The bar at the bottom of the screen always shows the buttons that do something.

| Context | Control | Action |
| --- | --- | --- |
| Map | Left stick, D-Pad or touch drag | Move the map under the aim |
| Map | Right-stick Y | Zoom |
| Map | `A` or touch | Open the object under the aim (its name shows at the bottom left) |
| Map | `ZR` / `ZL` | Next / previous of the 5 nearest missing visible objects (from Link when his position is known), with its hint or card open. Starts again from the nearest when the list changes |
| Map | `R` | Open the missing visible object nearest to the aim, gliding to it |
| Map | `L` | Center on Link, keeping the zoom |
| Map | Left-stick click | Reset the zoom and center on Link |
| Map | `X` | Open or close the legend |
| Map | `Y` | Switch Normal Mode / Master Mode when available |
| Map | `-` | With BotW running: read its progress again |
| Map | `+` | Exit |
| Card | `B` | Back |
| Card | `A` | Korok: the guide's photo full screen. Chest: the kind of content, then the item |
| Card | `Y` | Mark as found, while a save backup is shown |
| Panel | D-Pad left/right, or touch a title | Switch between the tabs Legend, Progress and Regions |
| Legend | D-Pad up/down, `A`, or touch | Select/toggle a category |
| Regions | D-Pad up/down, `A`, or touch | Show one region alone on the map, or the whole map again |

## High-resolution map (optional)

The app ships with a 3000x2500 map. When `sdmc:/switch/botw-unexplored/map-hd/` holds the map at 4x
(12000x10000) cut in 1024-pixel JPEG chunks, the app draws the chunks on screen over it from zoom 1.5 on,
loading them in the background as the map moves. The chunks are not distributed with the app.

- Files: `map-hd.txt` with the lines `BOTW_UNEXPLORED_MAP_HD`, `1` and `12000 10000 1024`, plus one
  `<column>_<row>.jpg` per chunk (`00_00.jpg` at the top left to `11_09.jpg`, edge chunks smaller), RGB.
- As an application (title override or forwarder) up to 40 chunks stay in memory (about 4 MB each). From the
  Album the app has little memory (79 MB free on the console with BotW running, 60 MB once the map is up), so each
  chunk is halved as soon as it is decoded (512 pixels, about 1 MB: the map at 2x), up to 24 stay, and one is loaded
  only while 32 MB stay free after it; where they do not, that part of the map stays as the built-in one. Zoomed in
  (2.5 and over), where a screen holds at most 3 x 3 chunks, the ones on screen are loaded again at full size, the
  nearest to the center first, while that memory rule allows (the halved one stays on screen until then). `log.txt`
  tells what it found (the `MEMORY:` and `MAP HD:` lines; at the end, the most chunks loaded at once and the least
  memory free).
- The textures do not come out of that memory: from the Album they have a small pool of their own, which panning
  zoomed in once filled with chunks at full size until new ones drew black. So the textures are weighed too, about
  4 MB at full size and 1 MB halved, up to 30 MB from the Album (a screen of 6 chunks at full size and 3 halved):
  past that, chunks off screen go first, and with none left the ones still halved on screen stay halved. A texture
  that fails anyway is dropped (the chunk keeps what it had) and no more are kept than at that moment. The last
  `MAP HD:` line also tells the most MB of textures loaded at once. The textures are weighed as the GPU holds them:
  RGB as RGBA (4 bytes a pixel) and a third more for the mipmaps, 5.3 MB at full size and 1.3 MB halved, up to
  38.5 MB from the Album (the console ran out near 40 MB).
- **BC1 chunks** (zayr26): `tools/map_dds.cpp` turns each `<column>_<row>.jpg` into a `<column>_<row>.dds` next to it,
  BC1 (DXT1) with every mipmap level, rows bottom up as the app loads its textures; BC1 keeps 4 bits a pixel, which
  the GPU reads as they are (0.7 MB a chunk instead of 5.3). When `map-hd/00_00.dds` is there and the GL driver takes
  BC1 (`GL_EXT_texture_compression_s3tc`), every chunk is loaded from its `.dds` at full size, from the Album too, up to
  40 at once (27 MB); a chunk without its `.dds` falls back to the JPEG. Without BC1 the JPEG chunks are used as above.
  The 120 files take 77 MB on the SD card.

  ```bash
  g++ -O2 -std=c++14 -Iinclude tools/map_dds.cpp -o map_dds
  for f in map-hd/*.jpg; do ./map_dds "$f" "${f%.jpg}.dds"; done
  ```

  The built-in map is in BC1 too (`romfs/map-lowres.dds`, made the same way from `romfs/map-lowres.png`, which stays
  for a driver without BC1): 5 MB on the GPU instead of 40, the whole time the app is open. `log.txt` says whether
  the driver takes BC1 (`GL:` line) and which textures came from a `.dds`.

## Quests on the map (optional)

`tools/gen_quests.py` reads a dump of the game (version 1.6.0, and the Expansion Pass for its quests) and writes the
file the app looks for:

```bash
python3 tools/gen_quests.py <romfs> [<dlc romfs>] > quests.txt
```

Copy it to `sdmc:/switch/botw-unexplored/quests.txt`. It holds where each quest is taken (its giver as placed in the
world's map units; failing that, its place or the first marker of its steps), the flags that tell whether it was
taken, is done or can be taken yet, and its English name, place and description. Since version 2 (zayr27) it also
holds the main quests and the steps of every quest: the flag each step sets when the quest reaches it (the step a
quest is at is the last one set), where the game's marker for the step points, and the quest log's text for it.
Releases before zayr27 read only version 1 files. It comes from the game's files, so
it is not distributed with the app. `tools/nxfmt.py` reads the formats (Yaz0, SARC, BYML, MSBT) with no
dependencies.

## Sharper icons (optional)

A PNG in `sdmc:/switch/botw-unexplored/icons/` with the name of an image at the top of romfs (for example
`shrine.png` or `hinox.png`) replaces it. It is drawn at the size of the built-in image, so it can be larger as long
as the aspect ratio is the same (`tower.png` and `fairy.png`, for one, can hold the game's own map icon of the
towers and its Fairy). These files are not distributed with the app. All images now use mipmaps, so they
stay clean when drawn smaller than their size.

## Saved data

The app stores its own files under `sdmc:/switch/botw-unexplored/`:

- `settings.txt` is a versioned settings file. Older settings files are accepted and rewritten in the current format. The categories added later (chests, walls) are kept at the end of the file, where a release without them does not look: it still reads the file.
- `manual_progress.dat` is a versioned manual-progress file. Entries are keyed by Switch profile, Normal/Master Mode, object type, and completion hash. A completion later confirmed by a readable game save removes the corresponding override. While it holds a mark on a chest the file is version 2, and with one on a wall version 3: a release that does not know those objects leaves such a file untouched (and shows no manual progress) instead of discarding it.

No file in this directory changes the actual BotW save.

## Build

Install devkitPro/devkitA64 and the Switch packages:

```bash
pacman -S switch-mesa switch-glad switch-glm switch-freetype
```

Then build:

```bash
make
```

`make DEBUG=1` enables per-frame OpenGL diagnostics. The release build does not poll `glGetError()` every frame.

The project uses `aarch64-none-elf-pkg-config` for FreeType when available and retains a tested fallback for older devkitPro installations.

## Tests and CI

Run the host-side checks with:

```bash
bash tests/run_host_tests.sh
```

They cover settings migration and validation, UTF-8 decoding, manual-progress profile/mode/type separation, object metadata mapping, data counts, duplicate completion hashes, the chest tables (world and shrines), the wall table, the tower and fountain flags, the memory rule of the high-resolution map and its BC1 files (`DDS_FILE` adds a real one), the counters of quests, compendium, armor and memories, the quests left to take by region, the table of regions, the f32 and array flags of the live reader, the Blood Moon estimate, the size of the icons at each zoom, the reader of
quests.txt and the step a quest is at (`QUESTS_FILE` adds a real one, `QUESTS_SAVE` a save to count its quests in
progress), and the reading of the Hero's Path files (`HERO_PATH_DIR` adds the tracker folder of a real save). GitHub Actions also builds the Switch `.nro` and uploads it as an artifact.

## Credits

- [lud99/botw-unexplored](https://github.com/lud99/botw-unexplored) — original application and codebase.
- [marcrobledo/savegame-editors](https://github.com/marcrobledo/savegame-editors), [MrCheeze/botw-waypoint-map](https://github.com/MrCheeze/botw-waypoint-map), [Zelda Dungeon](https://www.zeldadungeon.net/), and [d4mation/botw-unexplored-viewer](https://github.com/d4mation/botw-unexplored-viewer) — original data, research, and guide references.
- [zeldamods object map](https://objmap.zeldamods.org/) — positions, contents and save flags of the treasure chests, read from the game's map files (`tools/gen_chests.py` turns them into `source/ChestData.cpp`, `tools/gen_shrine_chests.py` does the same for the chests inside the shrines, and `tools/gen_walls.py` for the breakable walls), and the outline of the fifteen regions (`tools/gen_regions.py`).
- [leoetlino/botw](https://github.com/leoetlino/botw) — the game's data in readable form, from which `tools/gen_progress.py` takes the kind of each quest, the flags of its first and last step, who gives it and what it waits for, and the armor pieces with their levels of upgrade and which of them only amiibo give.
- [Hyrule Compendium API](https://gadhagod.github.io/Hyrule-Compendium-API/) — the category of each entry of the compendium, which the game's data does not tell.
- [savage13/heros_path](https://github.com/savage13/heros_path) (BSD-2-Clause), the [zephenryus notes](https://gist.github.com/zephenryus/e46c797ccecf6134d4245bb9f2e5e2a5) and [jinghaihan/botw-totk-atlas](https://github.com/jinghaihan/botw-totk-atlas) (MIT) — the format of the Hero's Path files and where its line breaks (`source/HeroPath.cpp`).
- Text: the console's own font (the HOME menu's), handed over by the system's pl service at run time, so no font file ships with the app.
- Icon: the Z is drawn with [The Wild Breath of Zelda](https://www.fontspace.com/the-wild-breath-of-zelda-font-f25653) by Chequered Ink (freeware).

BotW Unexplored is an unofficial homebrew project and is not affiliated with Nintendo.

## Looking at the interface on a PC

`tools/hostview/` builds the app for Linux (WSL works) against a small fake of libnx and renders it off screen with
Mesa, driven by a script of button presses, saving screenshots. See `tools/hostview/README.md`.
