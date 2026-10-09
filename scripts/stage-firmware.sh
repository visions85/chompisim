#!/usr/bin/env bash
# Copies a CHOMPI firmware's src/ folder into a build directory and applies the
# simulator patches. The upstream checkout is never modified.
#   stage-firmware.sh <firmware src dir> <staging dir> <patch dir>
set -euo pipefail
SRC="$1"; OUT="$2"; PATCHES="$3"
rm -rf "$OUT"; mkdir -p "$OUT"
cp -R "$SRC"/. "$OUT"/
rm -rf "$OUT/build"
applied=0
shopt -s nullglob
for p in "$PATCHES"/*.patch; do
  if patch -p1 -d "$OUT" --forward --silent < "$p"; then
    echo "applied $(basename "$p")"; applied=$((applied+1))
  else
    echo "WARNING: $(basename "$p") did not apply cleanly (firmware newer than the patch?)" >&2
  fi
done
# The firmware was developed on a case-insensitive file system: some #include
# lines spell a header differently from the file name (e.g. "Limiter.h" for
# limiter.h). Add symlinks under the spelled name so the build works on Linux.
linked=0
for name in $(grep -hoE '#include "[^"]+"' "$OUT"/*.h "$OUT"/*.cpp 2>/dev/null | sed -E 's/#include "([^"]+)"/\1/' | sort -u); do
  case "$name" in */*) continue;; esac
  if [ ! -e "$OUT/$name" ]; then
    match="$(find "$OUT" -maxdepth 1 -iname "$name" | head -1)"
    if [ -n "$match" ]; then ln -s "$(basename "$match")" "$OUT/$name"; linked=$((linked+1)); fi
  fi
done
[ "$linked" -gt 0 ] && echo "added $linked case-insensitive include link(s)"
echo "staged $(ls "$OUT" | wc -l) files, $applied patch(es)"
