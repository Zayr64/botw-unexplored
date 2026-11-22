#!/usr/bin/env bash
# Builds BotW Cozinha for the PC with tools/hostview's fake of libnx and runs it with a script of button presses,
# saving the screenshots the script asks for (see tools/hostview/README.md for the script commands).
#
#   cozinha/tools/hostview.sh <cook-data.txt> <BotW save folder> <script> <output folder> [closed|running]
#
# closed:  the save folder is "mounted" (game closed).
# running: BotW "runs": the save cannot be mounted; the folder's 0/game_data.sav is BotW Unexplored's copy on the SD.
# HOSTVIEW_SYSROOT: as in tools/hostview/build.sh.
set -euo pipefail
DATA="$1"; SAVE="$2"; SCRIPT="$3"; OUTDIR="$4"; MODE="${5:-closed}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
HV="$REPO/tools/hostview"
OUT="$HOME/hostview-build/cozinha"
LIBDIR="${HOSTVIEW_LIBDIR:-/usr/lib/x86_64-linux-gnu}"
mkdir -p "$OUT" "$OUTDIR"

FLAGS=(-std=gnu++17 -O1 -g -Wall -Wno-unused-function -D__SWITCH__ -I"$HV/shim" -I"$REPO/include" -I"$REPO/source"
       -I"$REPO/cozinha/source" -I"$REPO/cozinha/app")
if [[ -n "${HOSTVIEW_SYSROOT:-}" ]]; then
    FLAGS+=(-I"$HOSTVIEW_SYSROOT/usr/include" -I"$HOSTVIEW_SYSROOT/usr/include/freetype2")
else
    FLAGS+=(-I/usr/include/freetype2)
fi
sed 's#"Graphics/InitGL.h"#"HostInitGL.h"#' "$REPO/cozinha/app/CozinhaMain.cpp" > "$OUT/Main_host.cpp"
SRCS=("$OUT/Main_host.cpp" "$HV/host.cpp" "$REPO/cozinha/app/SaveSource.cpp" "$REPO/cozinha/app/CozinhaLog.cpp"
      "$REPO/source/Utf8.cpp" "$REPO/source/ImageOverride.cpp")
for f in "$REPO"/cozinha/source/*.cpp; do SRCS+=("$f"); done
for f in Font ShapeBatch Shader Vertex Texture2D stb_image; do SRCS+=("$REPO/source/Graphics/$f.cpp"); done
lib() { ls "$LIBDIR"/$1.so* 2>/dev/null | head -1; }
g++ "${FLAGS[@]}" -ffp-contract=off "${SRCS[@]}" "$(lib libEGL)" "$(lib libGL)" "$(lib libfreetype)" "$(lib libpng16)" \
    -lpthread -o "$OUT/app"

mkdir -p "$HOME/hostview-run"
RUN="$(mktemp -d "$HOME/hostview-run/cozinha-$MODE-XXXXXX")"
mkdir -p "$RUN/sdmc:/switch/botw-cozinha"
cp "$DATA" "$RUN/sdmc:/switch/botw-cozinha/cook-data.txt"
ln -s "$REPO/cozinha/romfs" "$RUN/romfs:"
case "$MODE" in
    closed) ln -s "$SAVE" "$RUN/save:" ;;
    running)
        mkdir -p "$RUN/sdmc:/switch/botw-unexplored/saves/4369 - 8738/0"
        cp "$SAVE/0/game_data.sav" "$RUN/sdmc:/switch/botw-unexplored/saves/4369 - 8738/0/"
        [[ -e "$SAVE/0/caption.sav" ]] && cp "$SAVE/0/caption.sav" "$RUN/sdmc:/switch/botw-unexplored/saves/4369 - 8738/0/"
        export HOSTVIEW_GAME_RUNNING=1 HOSTVIEW_APPLET=1 ;;
    *) echo "unknown mode $MODE"; exit 2 ;;
esac
cd "$RUN"
HOSTVIEW_SCRIPT="$SCRIPT" HOSTVIEW_OUT="$OUTDIR" LIBGL_ALWAYS_SOFTWARE=1 "$OUT/app" > "$OUTDIR/stdout-$MODE.txt" 2>&1 || {
    echo "the app exited with an error"; tail -20 "$OUTDIR/stdout-$MODE.txt"; exit 1; }
cp "sdmc:/switch/botw-cozinha/log.txt" "$OUTDIR/log-$MODE.txt" 2>/dev/null || true
echo "screenshots: $(grep -c 'hostview: wrote' "$OUTDIR/stdout-$MODE.txt" || true)   working folder: $RUN"
