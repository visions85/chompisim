#!/usr/bin/env bash
# Sparse-clones the CHOMPI open-source repository into third_party/CHOMPI:
# the firmware sources, the vendored DaisySP / coreJSON and the factory card of
# each requested firmware (all three by default; the TAPE card is 160 MB, the
# TEMPO card 30 MB, the WAVE card 2 MB). Usage: scripts/fetch-firmware.sh [wave tape tempo]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/CHOMPI"
FWS=("$@"); [ ${#FWS[@]} -eq 0 ] && FWS=(wave tape tempo)
if [ ! -d "$DEST/.git" ]; then
  git clone --filter=blob:none --no-checkout --depth 1 https://github.com/CHOMPI-Club/CHOMPI.git "$DEST"
  git -C "$DEST" sparse-checkout init --cone
fi
paths=()
for fw in "${FWS[@]}"; do
  paths+=("firmware/chompi-$fw/code/src" "firmware/chompi-$fw/code/libs/DaisySP" "firmware/chompi-$fw/code/libs/coreJSON")
  # the factory card folder is named <firmware>-<version>; look it up in the tree
  for card in $(git -C "$DEST" ls-tree --name-only HEAD firmware/card-profiles/ | grep "/$fw-"); do
    paths+=("$card")
  done
done
git -C "$DEST" sparse-checkout set "${paths[@]}"
git -C "$DEST" checkout
echo "CHOMPI sources ready in $DEST (${FWS[*]})"
ls -d "$DEST"/firmware/card-profiles/* 2>/dev/null | sed 's|^|  card: |'
