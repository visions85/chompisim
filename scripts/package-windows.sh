#!/usr/bin/env bash
# Gathers a Windows build into a folder to copy over: the executables, the DLLs
# they need (SDL2.dll, the MinGW runtime), the card scripts, the examples and
# the documentation.
#   scripts/package-windows.sh BUILD_DIR OUT_DIR [DLL folder ...]
# DLL folders are searched for the DLLs the executables import (SDL2's bin
# folder, MSYS2's /ucrt64/bin ...); the MinGW cross toolchain's folders are
# searched as well. DLLs found nowhere are taken to be Windows' own.
set -euo pipefail
BUILD="$1"; OUT="$2"; shift 2
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OBJDUMP="${OBJDUMP:-}"
if [ -z "$OBJDUMP" ]; then
  for o in x86_64-w64-mingw32-objdump objdump; do command -v "$o" >/dev/null 2>&1 && { OBJDUMP="$o"; break; }; done
fi
[ -n "$OBJDUMP" ] || { echo "no objdump found" >&2; exit 1; }
SEARCH=("$@")
for d in /usr/x86_64-w64-mingw32/lib /usr/x86_64-w64-mingw32/bin /usr/lib/gcc/x86_64-w64-mingw32/*-posix /usr/lib/gcc/x86_64-w64-mingw32/* \
         /opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/lib /opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/bin \
         /ucrt64/bin /mingw64/bin; do
  [ -d "$d" ] && SEARCH+=("$d")
done

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$BUILD"/*.exe "$OUT"/
# the DLLs, followed to their own DLLs
copy_dlls() {
  local exe="$1"
  for dll in $("$OBJDUMP" -p "$exe" | sed -n 's/^\s*DLL Name: \(.*\)$/\1/p'); do
    [ -e "$OUT/$dll" ] && continue
    local found=""
    for d in "${SEARCH[@]}"; do
      if [ -e "$d/$dll" ]; then found="$d/$dll"; break; fi
      # case-insensitive, for toolchains that spell them differently
      local hit; hit="$(find "$d" -maxdepth 1 -iname "$dll" 2>/dev/null | head -1)"
      if [ -n "$hit" ]; then found="$hit"; break; fi
    done
    if [ -n "$found" ]; then
      cp "$found" "$OUT/$dll"
      copy_dlls "$OUT/$dll"
    fi
  done
}
for exe in "$OUT"/*.exe; do copy_dlls "$exe"; done

cp "$ROOT/scripts/make-multi-card.py" "$ROOT/scripts/make-grain-card.py" "$OUT"/
cp -R "$ROOT/examples" "$OUT"/examples
cp "$ROOT/README.md" "$ROOT/LICENSE" "$ROOT/THIRD_PARTY.md" "$OUT"/
cat > "$OUT/RUN-ME.txt" <<'TXT'
chompi-sim for Windows

1. Get the factory cards: copy the card-profiles folder of the CHOMPI open-source
   bundle (github.com/CHOMPI-Club/CHOMPI, firmware/card-profiles) next to these
   files as "cards", so that cards\wave-1.0, cards\tape-2.0 and cards\tempo-1.0 exist.
   The firmware writes options.json and presets.json to the card it runs from.

2. From a command prompt in this folder:
     chompi-sim-gui.exe --cards cards              the instrument in a window
     chompi-sim-gui.exe --card cards\wave-1.0      one card: the firmware is read off it
     chompi-sim.exe --card cards\wave-1.0 --seconds 12 --script examples\phrase.txt --wav out.wav

   One card with every firmware on it (needs Python 3):
     python make-multi-card.py cards\multi --profiles cards
     chompi-sim-gui.exe --card cards\multi         hold a white key while it boots to choose

The README.md next to this file is the full documentation. The tour's
"seen" flag lives in %APPDATA%\chompi-sim\tour-seen.
TXT
echo "packaged into $OUT:"; ls "$OUT"
