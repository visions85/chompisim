# Cross-compiling for Windows (x86_64) with MinGW-w64, from Linux or macOS:
#   cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
#         -DCMAKE_PREFIX_PATH=/path/to/SDL2-2.x/x86_64-w64-mingw32
# scripts/build-windows.sh does this, fetching SDL2's MinGW package first.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(_triple x86_64-w64-mingw32)

# the POSIX-threads flavour of the compiler gives std::thread; Debian ships it as *-posix
find_program(_mingw_cxx NAMES ${_triple}-g++-posix ${_triple}-g++)
find_program(_mingw_cc NAMES ${_triple}-gcc-posix ${_triple}-gcc)
find_program(_mingw_rc NAMES ${_triple}-windres)
if(NOT _mingw_cxx OR NOT _mingw_cc)
  message(FATAL_ERROR "MinGW-w64 not found (${_triple}-g++). Debian/Ubuntu: apt install g++-mingw-w64-x86-64-posix; macOS: brew install mingw-w64")
endif()
set(CMAKE_C_COMPILER "${_mingw_cc}")
set(CMAKE_CXX_COMPILER "${_mingw_cxx}")
if(_mingw_rc)
  set(CMAKE_RC_COMPILER "${_mingw_rc}")
endif()

# look for headers and libraries in the cross root (and CMAKE_PREFIX_PATH), for programs on the host
set(CMAKE_FIND_ROOT_PATH "/usr/${_triple}" "/usr/local/${_triple}" "/opt/homebrew/opt/mingw-w64/toolchain-x86_64/${_triple}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
