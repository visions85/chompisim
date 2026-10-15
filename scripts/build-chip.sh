#!/usr/bin/env bash
# Builds the DRONE firmware for the instrument (STM32H750 on the Daisy Seed) with the
# GNU Arm Embedded toolchain, the way CHOMPI's own firmwares are built: an SRAM app
# for CHOMPI's bootloader, linked with the vendored libDaisy and DaisySP of the CHOMPI
# checkout (built here first when their archives are missing).
#   scripts/build-chip.sh [path to a CHOMPI code/libs folder]
# The result is firmware/drone/code/src/build/CHOMPI.bin; the linker prints how much of
# each memory region it takes. Needs arm-none-eabi-gcc (Debian: gcc-arm-none-eabi,
# libnewlib-arm-none-eabi, libstdc++-arm-none-eabi-newlib).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LIBS="${1:-$ROOT/third_party/CHOMPI/firmware/chompi-wave/code/libs}"
LIBS="$(cd "$LIBS" && pwd)"
command -v arm-none-eabi-gcc >/dev/null || { echo "arm-none-eabi-gcc not found: install the GNU Arm Embedded toolchain" >&2; exit 1; }
# scripts/fetch-firmware.sh checks out only what the simulator needs; the chip build also wants
# the whole of libDaisy (its HAL, middlewares, startup code and linker scripts)
if [ ! -f "$LIBS/libDaisy/core/Makefile" ]; then
  REPO="$(cd "$LIBS/../../../.." && pwd)"
  if [ -d "$REPO/.git" ]; then
    git -C "$REPO" sparse-checkout add "${LIBS#"$REPO"/}/libDaisy"
    git -C "$REPO" checkout
  fi
  [ -f "$LIBS/libDaisy/core/Makefile" ] || { echo "libDaisy not found under $LIBS" >&2; exit 1; }
fi
[ -f "$LIBS/libDaisy/build/libdaisy.a" ] || make -C "$LIBS/libDaisy" -j"$(nproc)"
[ -f "$LIBS/DaisySP/build/libdaisysp.a" ] || make -C "$LIBS/DaisySP" -j"$(nproc)"
make -C "$ROOT/firmware/drone/code/src" LIBS_DIR="$LIBS" -j"$(nproc)" | grep -E "Memory region|RAM|SRAM|FLASH" || true
ls -l "$ROOT/firmware/drone/code/src/build/CHOMPI.bin"
