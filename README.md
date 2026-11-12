# BotW Unexplored

BotW Unexplored is a Nintendo Switch homebrew map companion for *The Legend of Zelda: Breath of the Wild*. It reads the selected local save and shows collectible progress directly on the console. Opened from the Album while BotW 1.6.0 is running, it reads the progress (and where Link is) straight from the game's memory, read only, through Atmosphere's cheat service; a save backup made earlier is the fallback.

This repository is the maintained fork of [lud99/botw-unexplored](https://github.com/lud99/botw-unexplored). The current fork release is **v2.2.11**.

## Features

- Koroks, shrines, DLC shrines, locations, Hinoxes, Taluses, and Moldugas.
- **Treasure chests** of the world map that are still closed, in two categories that start switched off: `Chests` and `Special chests` (armor, key items and horse gear, plus weapons, bows, shields and materials found in at most two chests of the whole map). A chest card says only that the chest is not opened and how far it is; `A` tells the kind of thing inside, and `A` again the item itself. Chests of the Expansion Pass show once the save has a sign of it (one of them opened, the Champions' Ballad or a Master Mode save); chests that only exist in Master Mode show only there. Chests inside shrines and Divine Beasts are not on the map (a shrine card tells when one is left).
- **Live progress while playing**: with BotW 1.6.0 running behind the app (opened from the Album), the game's own flags are read from its memory every time the app opens, and again with `-`. Nothing is written to the game. The chip at the top right tells where the progress comes from: `Live from the game`, `Save backup` (the memory could not be read; the reason shows for a few seconds and goes to `log.txt`) or `Save`.
- Normal Mode and Master Mode, including save-backup support when BotW is running.
- A bar at the bottom shows what each button does in the current screen, and the name of the object under the aim.
- Object cards: type, name, what is done and what is left (a hollow dot), distance and direction from Link, game coordinates. Korok cards add the guide's photo (`A` enlarges it) and the hint.
- The legend shows the map completion (computed like the game: Korok seeds, discovered shrines and locations over 1207) and a progress bar per category.
- The map shows only what is still missing. Icons shrink on the overview, where they pile up; location names have a dark edge.
- While the legend or a card is open the map slides to the right, so that the selected object stays in view, inside a ring.
- Per-profile and per-mode manual progress (`Y`) for every supported object type, only while a save backup is shown. It never modifies the BotW save.
- `ZR` / `ZL` step forward / back through the 5 nearest missing visible objects, opening the Korok hint or the card of each one.
- "You are here": a marker where Link is (right now when read from the game, otherwise the last save); the map opens there and `L` glides back to it. Inside a shrine the marker is on the shrine.
- Shrine cards show whether the shrine is completed and how many of its chests are opened (`Chests 2/3`); a discovered shrine with something left stays on the map.
- The same for the 15 towers and the great fairy fountains: one that was visited but not activated (or whose fairy still sleeps) stays on the map, and its card says so.
- The shrines of the Champions' Ballad show only once that quest has begun, when they can exist in the world. Before that the legend counts 120 shrines.
- The most recent save slot is picked by the save time in `caption.sav`, so loading an older save is followed.
- Optional high-resolution map (4x) when zoomed in, read from the SD card (see below).
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
| Legend | D-Pad, `A`, or touch | Select/toggle a category |

## High-resolution map (optional)

The app ships with a 3000x2500 map. When `sdmc:/switch/botw-unexplored/map-hd/` holds the map at 4x
(12000x10000) cut in 1024-pixel JPEG chunks, the app draws the chunks on screen over it from zoom 1.5 on,
loading them in the background as the map moves. The chunks are not distributed with the app.

- Files: `map-hd.txt` with the lines `BOTW_UNEXPLORED_MAP_HD`, `1` and `12000 10000 1024`, plus one
  `<column>_<row>.jpg` per chunk (`00_00.jpg` at the top left to `11_09.jpg`, edge chunks smaller), RGB.
- Only used when the app runs as an application (title override or forwarder). From the Album, which has
  less memory, the app keeps the built-in map. Up to 40 chunks stay in memory (about 4 MB each).

## Sharper icons (optional)

A PNG in `sdmc:/switch/botw-unexplored/icons/` with the name of an image at the top of romfs (for example
`shrine.png` or `hinox.png`) replaces it. It is drawn at the size of the built-in image, so it can be larger as long
as the aspect ratio is the same. These files are not distributed with the app. All images now use mipmaps, so they
stay clean when drawn smaller than their size.

## Saved data

The app stores its own files under `sdmc:/switch/botw-unexplored/`:

- `settings.txt` is a versioned settings file. Older settings files are accepted and rewritten in the current format. The chest categories are kept at the end of the file, where a release without them does not look: it still reads the file.
- `manual_progress.dat` is a versioned manual-progress file. Entries are keyed by Switch profile, Normal/Master Mode, object type, and completion hash. A completion later confirmed by a readable game save removes the corresponding override. While it holds a mark on a chest the file is version 2, which a release without chests leaves untouched (and without manual progress) instead of discarding.

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

They cover settings migration and validation, UTF-8 decoding, manual-progress profile/mode/type separation, object metadata mapping, data counts, duplicate completion hashes, the chest tables (world and shrines), and the tower and fountain flags. GitHub Actions also builds the Switch `.nro` and uploads it as an artifact.

## Credits

- [lud99/botw-unexplored](https://github.com/lud99/botw-unexplored) — original application and codebase.
- [marcrobledo/savegame-editors](https://github.com/marcrobledo/savegame-editors), [MrCheeze/botw-waypoint-map](https://github.com/MrCheeze/botw-waypoint-map), [Zelda Dungeon](https://www.zeldadungeon.net/), and [d4mation/botw-unexplored-viewer](https://github.com/d4mation/botw-unexplored-viewer) — original data, research, and guide references.
- [zeldamods object map](https://objmap.zeldamods.org/) — positions, contents and save flags of the treasure chests, read from the game's map files (`tools/gen_chests.py` turns them into `source/ChestData.cpp`, and `tools/gen_shrine_chests.py` does the same for the chests inside the shrines).
- Icon: the Z is drawn with [The Wild Breath of Zelda](https://www.fontspace.com/the-wild-breath-of-zelda-font-f25653) by Chequered Ink (freeware).

BotW Unexplored is an unofficial homebrew project and is not affiliated with Nintendo.

## Looking at the interface on a PC

`tools/hostview/` builds the app for Linux (WSL works) against a small fake of libnx and renders it off screen with
Mesa, driven by a script of button presses, saving screenshots. See `tools/hostview/README.md`.
