# BotW Unexplored

BotW Unexplored is a Nintendo Switch homebrew map companion for *The Legend of Zelda: Breath of the Wild*. It reads the selected local save (or a previously created backup while the game is running) and shows collectible progress directly on the console.

This repository is the maintained fork of [lud99/botw-unexplored](https://github.com/lud99/botw-unexplored). The current fork release is **v2.2.11**.

## Features

- Koroks, shrines, DLC shrines, locations, Hinoxes, Taluses, and Moldugas.
- Normal Mode and Master Mode, including save-backup support when BotW is running.
- An object information card with type, name where available, game coordinates, icon, and completion state.
- Existing Korok hints, guide images, and route rendering remain available through touch selection.
- The map shows only what is still missing.
- Per-profile and per-mode manual progress for every supported object type. It never modifies the BotW save.
- Controller cursor: D-Pad moves the cursor, `A` opens the nearest visible object, and `ZR` / `ZL` step forward / back through the 5 nearest missing visible objects, opening the Korok hint or the card of each one.
- Map completion in the legend, computed like the game (Korok seeds, discovered shrines and locations over 1207).
- "You are here": a marker at the position of the last save; the map opens there, `L` glides back to it, and `ZR` / `ZL` measure distances from it.
- Shrine cards show whether the shrine is completed and all of its chests are open; a discovered shrine with something left stays in the `Missing` view.
- The most recent save slot is picked by the save time in `caption.sav`, so loading an older save is followed.
- Optional high-resolution map (4x) when zoomed in, read from the SD card (see below).
- Persisted camera, zoom, and legend visibility. Legacy settings are migrated safely.

## Installation

1. Download `botw-unexplored.nro` from [Releases](https://github.com/sklart/botw-unexplored/releases).
2. Copy it to `sdmc:/switch/botw-unexplored/botw-unexplored.nro`.
3. Start it from the Homebrew Menu and select the Switch profile that owns the BotW save.

For backup-based use while BotW is running, start BotW Unexplored once with the game closed first. This creates a backup of the selected save.

## Controls

| Context | Control | Action |
| --- | --- | --- |
| Map | Left stick or touch drag | Move the map |
| Map | Right-stick Y | Zoom |
| Map | `X` | Open or close the legend / close an information panel |
| Map | `Y` | Switch Normal Mode / Master Mode when available |
| Map | `-` | Choose another profile |
| Map | `+` | Exit |
| Map | D-Pad | Move the controller cursor |
| Map | `A` | Open the full Korok guide for a Korok, or ObjectInfo for another object |
| Map | `ZR` / `ZL` | Next / previous of the 5 nearest missing visible objects (from Link when the save has a position), with its hint or card open. Starts again from the nearest when the list changes |
| Map | `L` | Center on Link, keeping the zoom |
| Map | Left-stick click | Reset the zoom and center on Link |
| Object info | `B` | Mark a missing object as found while using a backup |
| Legend | D-Pad, `A`, or touch | Select/toggle a category |
| Korok hint | `B` | Mark the Korok as found while using a backup and close the hint |

## High-resolution map (optional)

The app ships with a 3000x2500 map. When `sdmc:/switch/botw-unexplored/map-hd/` holds the map at 4x
(12000x10000) cut in 1024-pixel JPEG chunks, the app draws the chunks on screen over it from zoom 1.5 on,
loading them in the background as the map moves. The chunks are not distributed with the app.

- Files: `map-hd.txt` with the lines `BOTW_UNEXPLORED_MAP_HD`, `1` and `12000 10000 1024`, plus one
  `<column>_<row>.jpg` per chunk (`00_00.jpg` at the top left to `11_09.jpg`, edge chunks smaller), RGB.
- Only used when the app runs as an application (title override or forwarder). From the Album, which has
  less memory, the app keeps the built-in map. Up to 40 chunks stay in memory (about 4 MB each).

## Saved data

The app stores its own files under `sdmc:/switch/botw-unexplored/`:

- `settings.txt` is a versioned settings file. Older settings files are accepted and rewritten in the current format.
- `manual_progress.dat` is a versioned manual-progress file. Entries are keyed by Switch profile, Normal/Master Mode, object type, and completion hash. A completion later confirmed by a readable game save removes the corresponding override.

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

They cover settings migration and validation, UTF-8 decoding, manual-progress profile/mode/type separation, object metadata mapping, data counts, and duplicate completion hashes. GitHub Actions also builds the Switch `.nro` and uploads it as an artifact.

## Credits

- [lud99/botw-unexplored](https://github.com/lud99/botw-unexplored) — original application and codebase.
- [marcrobledo/savegame-editors](https://github.com/marcrobledo/savegame-editors), [MrCheeze/botw-waypoint-map](https://github.com/MrCheeze/botw-waypoint-map), [Zelda Dungeon](https://www.zeldadungeon.net/), and [d4mation/botw-unexplored-viewer](https://github.com/d4mation/botw-unexplored-viewer) — original data, research, and guide references.
- Icon: the Z is drawn with [The Wild Breath of Zelda](https://www.fontspace.com/the-wild-breath-of-zelda-font-f25653) by Chequered Ink (freeware).

BotW Unexplored is an unofficial homebrew project and is not affiliated with Nintendo.
