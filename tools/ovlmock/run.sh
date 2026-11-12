#!/usr/bin/env bash
# Builds the overlay against a fake of libultrahand (Tesla) and libnx and runs one script of button presses,
# drawing its screens into images. For layout only: the console uses another font and draws its own frame.
#
#   run.sh <script> <output folder> <tag> [overlay arguments...] -- [NAME=value ...]
#
# Overlay arguments: none for the list, "-guide <hash> -filter <n>" for the guide.
# NAME=value: OVLMOCK_LIVE_SAVE=<game_data.sav> gives the "running game" a memory (unset: no game running);
#             BACKUP_SAVE=<game_data.sav> is the save backup of the app on the SD card;
#             OVLMOCK_BACKDROP=<1280x720 image> goes under the overlay. mock.cpp lists the others.
# HOSTVIEW_SYSROOT: optional folder with the FreeType and libpng headers (see tools/hostview/README.md).
set -euo pipefail
SCRIPT="$1"; OUTDIR="$2"; TAG="$3"; shift 3
ARGS=()
while [[ $# -gt 0 && "$1" != "--" ]]; do ARGS+=("$1"); shift; done
[[ $# -gt 0 ]] && shift
for assignment in "$@"; do export "$assignment"; done

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
LIBDIR="${HOSTVIEW_LIBDIR:-/usr/lib/x86_64-linux-gnu}"
OUT="$HOME/hostview-build/ovlmock"
mkdir -p "$OUT" "$OUTDIR" "$HOME/hostview-run"

FLAGS=(-std=gnu++20 -O1 -g -w -DAPP_VERSION='"mock"' -I"$HERE/include" -I"$REPO/overlay/include" -I"$REPO/source"
       -I"$REPO/include" -I"$REPO/tests")
if [[ -n "${HOSTVIEW_SYSROOT:-}" ]]; then
    FLAGS+=(-I"$HOSTVIEW_SYSROOT/usr/include" -I"$HOSTVIEW_SYSROOT/usr/include/freetype2")
else
    FLAGS+=(-I/usr/include/freetype2)
fi
lib() { ls "$LIBDIR"/$1.so* 2>/dev/null | head -1; }
g++ "${FLAGS[@]}" "$REPO/overlay/source/main.cpp" "$REPO/overlay/source/shared_manual_progress.cpp" \
    "$REPO/overlay/source/shared_live_game_data.cpp" "$HERE/mock.cpp" "$(lib libfreetype)" "$(lib libpng16)" -o "$OUT/ovl"

# A fresh working folder with what the overlay sees as sdmc:/.
RUN="$(mktemp -d "$HOME/hostview-run/ovl-$TAG-XXXXXX")"
mkdir -p "$RUN/sdmc:/switch/botw-unexplored"
if [[ -n "${BACKUP_SAVE:-}" ]]; then
    mkdir -p "$RUN/sdmc:/switch/botw-unexplored/saves/4369 - 8738/0"
    cp "$BACKUP_SAVE" "$RUN/sdmc:/switch/botw-unexplored/saves/4369 - 8738/0/game_data.sav"
fi
cd "$RUN"
OVLMOCK_SCRIPT="$SCRIPT" OVLMOCK_OUT="$OUTDIR" OVLMOCK_FONT="${OVLMOCK_FONT:-$REPO/romfs/arial.ttf}" "$OUT/ovl" ovl "${ARGS[@]}" \
    > "$OUTDIR/stdout-$TAG.txt" 2>&1 || { echo "the overlay exited with an error"; tail -20 "$OUTDIR/stdout-$TAG.txt"; exit 1; }
cp "$RUN/sdmc:/switch/botw-unexplored/overlay-sync.log" "$OUTDIR/sync-$TAG.log" 2>/dev/null || true
echo "screenshots: $(grep -c 'ovlmock: wrote' "$OUTDIR/stdout-$TAG.txt" || true)   $(grep 'next overlay' "$OUTDIR/stdout-$TAG.txt" || true)"
