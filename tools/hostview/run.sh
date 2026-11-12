#!/usr/bin/env bash
# Runs the host build with a script of button presses and saves the screenshots it asks for. See README.md.
#
#   run.sh <build name> <script file> <output folder> [mode] [NAME=value ...]
#
# Modes: closed            the save is "mounted" (game closed)
#        running           BotW "runs" behind the app: the save cannot be mounted; a backup is on the SD card
#        running-nobackup  the same, without a backup
#        nosave            the profile has no BotW save
# HOSTVIEW_SAVE_DIR: a BotW save folder (with 0/game_data.sav ...), needed by every mode except nosave.
# HOSTVIEW_SD_DIR:   optional folder copied into sdmc:/switch/botw-unexplored (icons/, map-hd/, settings.txt...).
# Extra NAME=value arguments go to the app, e.g. HOSTVIEW_LIVE_SAVE=<game_data.sav> to give the running
# game a memory to read (see extra/HostCheatService.cpp for the others).
set -euo pipefail
NAME="$1"; SCRIPT="$2"; OUTDIR="$3"; MODE="${4:-closed}"
shift 3; [[ $# -gt 0 ]] && shift
for assignment in "$@"; do export "$assignment"; done

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SAVE="${HOSTVIEW_SAVE_DIR:-}"
if [[ "$MODE" != "nosave" && ! -e "$SAVE/0/game_data.sav" ]]; then
    echo "set HOSTVIEW_SAVE_DIR to a BotW save folder (with 0/game_data.sav)"; exit 2
fi

# A fresh working folder with what the app sees as romfs:/, sdmc:/ and save:/.
mkdir -p "$HOME/hostview-run" "$OUTDIR"
RUN="$(mktemp -d "$HOME/hostview-run/$NAME-$MODE-XXXXXX")"
APPDIR="$RUN/sdmc:/switch/botw-unexplored"
mkdir -p "$APPDIR"
ln -s "$REPO/romfs" "$RUN/romfs:"
if [[ -n "${HOSTVIEW_SD_DIR:-}" ]]; then cp -r "$HOSTVIEW_SD_DIR"/. "$APPDIR/"; fi
case "$MODE" in
    closed)
        ln -s "$SAVE" "$RUN/save:" ;;
    running)
        # The backup the app makes when it runs with the game closed (profile 4369 - 8738 is the fake user).
        mkdir -p "$APPDIR/saves/4369 - 8738/0"
        cp "${HOSTVIEW_BACKUP_SAVE:-$SAVE/0/game_data.sav}" "$APPDIR/saves/4369 - 8738/0/game_data.sav"
        export HOSTVIEW_GAME_RUNNING=1 HOSTVIEW_APPLET=1 ;;
    running-nobackup)
        export HOSTVIEW_GAME_RUNNING=1 HOSTVIEW_APPLET=1 ;;
    nosave)
        export HOSTVIEW_NO_SAVE=1 ;;
    *)
        echo "unknown mode $MODE"; exit 2 ;;
esac

LOG="$OUTDIR/stdout-${TAG:-$MODE}.txt"
cd "$RUN"
if ! HOSTVIEW_SCRIPT="$SCRIPT" HOSTVIEW_OUT="$OUTDIR" LIBGL_ALWAYS_SOFTWARE=1 "$HOME/hostview-build/$NAME/app" > "$LOG" 2>&1; then
    echo "the app exited with an error"; tail -20 "$LOG"; exit 1
fi
cp "$APPDIR/log.txt" "$OUTDIR/log-${TAG:-$MODE}.txt" 2>/dev/null || true
echo "screenshots: $(grep -c 'hostview: wrote' "$LOG" || true)   working folder: $RUN"
