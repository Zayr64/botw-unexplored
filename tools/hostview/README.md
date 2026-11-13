# hostview: the interface on a PC

The Switch build needs devkitPro and a console to look at. This folder builds the same sources for Linux (WSL
works) against a small fake of libnx, renders them off screen with Mesa and saves screenshots, driven by a
script of button presses. It is for checking layout and flows, not performance.

What is faked (`shim/`, `host.cpp`): the controller and the touch screen (from the script), the profile, the
save mount (a folder) and EGL (an off-screen 1280x720 surface). `extra/HostCheatService.cpp` fakes Atmosphere's
cheat service with a "running game" whose memory is built from a save file, so the live read of the game's
flags runs for real against the layouts in `source/LiveGameData.cpp`.

## Requirements

`g++`, Mesa (software rendering is enough) and the headers of glm, FreeType, EGL/OpenGL and libpng:

```bash
sudo apt-get install g++ libglm-dev libfreetype-dev libegl-dev libgl-dev libpng-dev
```

Without root, download the packages and extract them somewhere, then point `HOSTVIEW_SYSROOT` at it:

```bash
mkdir -p ~/hostview-sysroot/debs && cd ~/hostview-sysroot/debs
apt-get download libglm-dev libfreetype-dev libegl-dev libgl-dev libopengl-dev libglx-dev libpng-dev
for f in *.deb; do dpkg -x "$f" ../root; done
export HOSTVIEW_SYSROOT=~/hostview-sysroot/root
```

## Use

```bash
tools/hostview/build.sh
HOSTVIEW_SAVE_DIR=/path/to/a/botw/save tools/hostview/run.sh app tools/hostview/scripts/tour.txt /tmp/shots
```

The save folder is a copy of a BotW save (`0/game_data.sav`, `0/caption.sav`, ...). Modes and variables are
described at the top of `run.sh`; `host.cpp` lists the script commands:

```text
wait 10            ten frames
press ZR           a button for one frame (A B X Y L R ZL ZR LS RS PLUS MINUS UP DOWN LEFT RIGHT)
hold RIGHT 60      a button held for 60 frames
lstick 1 0 30      left stick to the right for 30 frames (rstick: zoom)
touch 900 300 2    a finger at pixel 900,300 for two frames
shot 06-korok      save <output folder>/06-korok.png
```

`scripts/tour.txt` walks through the main screens, `scripts/tour-chests.txt` through the chests (legend, map,
card and what `A` reveals) and `scripts/tour-walls.txt` through the breakable walls.

The high-resolution map needs `HOSTVIEW_SD_DIR` with a `map-hd/` folder. In the running modes the app behaves
as opened from the Album, where it loads a chunk only while memory allows: `HOSTVIEW_FREE_MB=400` lets it,
`HOSTVIEW_FREE_MB=50` shows the fallback to the built-in map.

The game running behind the app, with its memory read:

```bash
HOSTVIEW_SAVE_DIR=/path/to/save tools/hostview/run.sh app tools/hostview/scripts/tour-live.txt /tmp/shots running \
    HOSTVIEW_LIVE_SAVE=/path/to/newer/game_data.sav HOSTVIEW_LIVE_POS=-1105.5,201.0,1880.2
```
