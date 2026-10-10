#!/usr/bin/env bash
# Cross-builds the simulator for Windows (x86_64) with MinGW-w64, from Linux or
# macOS, and packs it into dist/chompi-sim-windows (and a zip of it).
#   scripts/build-windows.sh [path to CHOMPI checkout]
# Needs the MinGW-w64 compiler (Debian/Ubuntu: apt install g++-mingw-w64-x86-64-posix;
# macOS: brew install mingw-w64), cmake, curl and tar. SDL2's MinGW development
# package is downloaded into third_party/SDL2-mingw the first time.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="${1:-$ROOT/third_party/CHOMPI}"
SDL_VERSION="${SDL_VERSION:-2.32.2}"
SDL_ROOT="$ROOT/third_party/SDL2-mingw/SDL2-$SDL_VERSION/x86_64-w64-mingw32"
if [ ! -d "$SDL_ROOT" ]; then
  mkdir -p "$ROOT/third_party/SDL2-mingw"
  url="https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-devel-$SDL_VERSION-mingw.tar.gz"
  echo "fetching $url"
  curl -L --fail -o "$ROOT/third_party/SDL2-mingw/sdl2.tar.gz" "$url"
  tar -xzf "$ROOT/third_party/SDL2-mingw/sdl2.tar.gz" -C "$ROOT/third_party/SDL2-mingw"
fi
cmake -S "$ROOT" -B "$ROOT/build-windows" -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64-x86_64.cmake" \
      -DCMAKE_PREFIX_PATH="$SDL_ROOT" -DCHOMPI_REPO_DIR="$REPO" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build-windows" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
"$ROOT/scripts/package-windows.sh" "$ROOT/build-windows" "$ROOT/dist/chompi-sim-windows" "$SDL_ROOT/bin"
(cd "$ROOT/dist" && rm -f chompi-sim-windows.zip && python3 -c "import shutil; shutil.make_archive('chompi-sim-windows', 'zip', '.', 'chompi-sim-windows')")
echo "zip: $ROOT/dist/chompi-sim-windows.zip"
