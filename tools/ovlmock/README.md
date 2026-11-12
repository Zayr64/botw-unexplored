# ovlmock: the overlay's screens on a PC

The overlay needs libultrahand and a console. `run.sh` builds `overlay/source/main.cpp` against a small fake of
libultrahand and libnx (`include/`, `mock.cpp`) and draws its list and its guide into images, driven by a script
of button presses. The fake cheat service gives the "running game" a memory built from a save file (the same
fake as `tools/hostview`), so the live read of the flags and the guide's change of target can be followed.

It is for layout and flows: the console uses Nintendo's font (about 3% wider than Arial at these sizes) and
libultrahand draws the real frame.

```bash
tools/ovlmock/run.sh tools/ovlmock/scripts/list.txt /tmp/ovl list -- \
    BACKUP_SAVE=/path/to/game_data.sav OVLMOCK_LIVE_SAVE=/path/to/newer/game_data.sav
tools/ovlmock/run.sh tools/ovlmock/scripts/guide.txt /tmp/ovl guide -guide 1615115873 -filter 1 -- \
    BACKUP_SAVE=/path/to/game_data.sav OVLMOCK_LIVE_SAVE=/path/to/newer/game_data.sav
```

Script commands: `wait N`, `press KEY`, `hold KEY N`, `pos X Y Z` (where Link is), `setflag HASH` (the game
collects an object) and `shot NAME`. Requirements as in `tools/hostview/README.md` (FreeType and libpng).
