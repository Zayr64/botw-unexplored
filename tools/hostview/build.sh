#!/usr/bin/env bash
# Builds the app for the host (Linux or WSL) with the libnx fake in shim/. See README.md.
#
#   build.sh [build name]
#
# HOSTVIEW_SYSROOT: optional folder with the headers the system lacks (usr/include with glm, freetype2,
#                   EGL, GL, KHR, png.h), for machines where the -dev packages cannot be installed.
# Objects are kept per source file in ~/hostview-build/<name>.
set -euo pipefail
NAME="${1:-app}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$HOME/hostview-build/$NAME"
LIBDIR="${HOSTVIEW_LIBDIR:-/usr/lib/x86_64-linux-gnu}"
mkdir -p "$OUT/obj"

FLAGS=(-std=gnu++17 -O1 -g -w -D__SWITCH__ -I"$HERE/shim" -I"$REPO/include" -I"$REPO/source" -I"$REPO/tests")
if [[ -n "${HOSTVIEW_SYSROOT:-}" ]]; then
    FLAGS+=(-I"$HOSTVIEW_SYSROOT/usr/include" -I"$HOSTVIEW_SYSROOT/usr/include/freetype2")
else
    FLAGS+=(-I/usr/include/freetype2)
fi

# Main.cpp as it is, except for the EGL setup, which becomes an off-screen surface.
sed 's#"Graphics/InitGL.h"#"HostInitGL.h"#' "$REPO/source/Main.cpp" > "$OUT/Main_host.cpp.new"
if ! cmp -s "$OUT/Main_host.cpp.new" "$OUT/Main_host.cpp" 2>/dev/null; then mv "$OUT/Main_host.cpp.new" "$OUT/Main_host.cpp"; fi

SRCS=("$OUT/Main_host.cpp" "$HERE/host.cpp")
for f in "$REPO"/source/*.cpp "$REPO"/source/Graphics/*.cpp "$HERE"/extra/*.cpp; do
    [[ "$(basename "$f")" == "Main.cpp" ]] && continue
    SRCS+=("$f")
done

# A header changed since the last build: compile everything again.
STAMP="$OUT/stamp"
REBUILD=0
if [[ ! -e "$STAMP" ]]; then
    REBUILD=1
elif [[ -n "$(find "$REPO/source" "$REPO/tests" "$HERE/shim" \( -name '*.h' -o -name '*.hpp' \) -newer "$STAMP" | head -1)" ]]; then
    REBUILD=1
fi

OBJS=()
pids=()
fail=0
for f in "${SRCS[@]}"; do
    o="$OUT/obj/$(echo "$f" | md5sum | cut -c1-8)-$(basename "$f" .cpp).o"
    OBJS+=("$o")
    if [[ "$REBUILD" == 1 || ! -e "$o" || "$f" -nt "$o" ]]; then
        ( g++ "${FLAGS[@]}" -c "$f" -o "$o" || { echo "FAILED $f"; exit 1; } ) &
        pids+=($!)
        if (( ${#pids[@]} >= 10 )); then wait "${pids[0]}" || fail=1; pids=("${pids[@]:1}"); fi
    fi
done
for p in "${pids[@]:-}"; do [[ -n "$p" ]] && { wait "$p" || fail=1; }; done
[[ "$fail" == 1 ]] && { echo "compile failed"; exit 1; }
touch "$STAMP"

# The runtime libraries are there even when the -dev packages (and their .so links) are not.
lib() { ls "$LIBDIR"/$1.so* 2>/dev/null | head -1; }
g++ "${OBJS[@]}" "$(lib libEGL)" "$(lib libGL)" "$(lib libfreetype)" "$(lib libpng16)" -lpthread -o "$OUT/app"
echo "built $OUT/app"
