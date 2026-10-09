#!/usr/bin/env bash
# Builds the headless simulator, boots the WAVE factory card, plays a note from
# the keybed and one over MIDI, and checks that audio, MIDI out and LEDs react.
#   scripts/smoke-test.sh [path to CHOMPI checkout]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="${1:-$ROOT/third_party/CHOMPI}"
BUILD="$ROOT/build-smoke"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cmake -S "$ROOT" -B "$BUILD" -DCHOMPI_REPO_DIR="$REPO" -DCHOMPI_FIRMWARES=wave -DCHOMPI_SIM_BUILD_GUI=OFF >/dev/null
cmake --build "$BUILD" -j4 --target chompi-sim-wave chompi-sim-selftest >/dev/null

# the firmware rewrites options.json / presets.json, so work on a copy of the factory card
cp -R "$REPO/firmware/card-profiles/wave-1.0" "$WORK/card"
"$BUILD/chompi-sim-selftest" "$WORK/card"

cat > "$WORK/script.txt" <<'SCRIPT'
8.0 note 12 1.0        # keybed: middle key for one second
9.5 midi 90 3C 7F      # MIDI in: note on
10.5 midi 80 3C 00     # MIDI in: note off
10.0 leds
SCRIPT
OUT="$("$BUILD/chompi-sim-wave" --card "$WORK/card" --seconds 11.5 --script "$WORK/script.txt" --wav "$WORK/out.wav" --leds "$WORK/leds.txt" --quiet)"
echo "$OUT" | head -2
PEAK="$(echo "$OUT" | sed -n 's/.*peak \([0-9.]*\).*/\1/p' | head -1)"
awk -v p="$PEAK" 'BEGIN { if (p + 0 < 0.001) { print "FAIL: no audio rendered"; exit 1 } }'
echo "$OUT" | grep -q "midi out: 90 3C 7F 80 3C 00" || { echo "FAIL: expected MIDI note on/off for the keybed press"; exit 1; }
grep -q "t=10 keys: 0,0,0 0,0,0 0,0,0 0,0,0 0,0,0 0,0,0 0,0,0 0,0,0 0,0,0 0,0,0 0,0,0 0,0,0 25[0-9],25[0-9],25[0-9]" "$WORK/leds.txt" || { echo "FAIL: key LED not lit for the MIDI note"; exit 1; }
echo "SMOKE TEST OK"
