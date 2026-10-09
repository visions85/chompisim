cmake_minimum_required(VERSION 3.16)
project(chompi_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)   # the firmware relies on GNU extensions (designated initialisers etc.)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(CHOMPI_SIM_BUILD_GUI "Build the SDL2 front-end" ON)
option(CHOMPI_SIM_STUB "Build the GUI against a stand-in core instead of the firmware (front-end development only)" OFF)
set(CHOMPI_FIRMWARE "wave" CACHE STRING "Firmware to simulate: wave, tape, tempo, or an absolute path to a <firmware>/code folder")
set(CHOMPI_REPO_DIR "${CMAKE_SOURCE_DIR}/third_party/CHOMPI" CACHE PATH "Checkout of github.com/CHOMPI-Club/CHOMPI (see scripts/fetch-firmware.sh)")

find_package(Threads REQUIRED)

# The firmware and the libDaisy headers copied into the shim carry GCC-only
# attributes such as __attribute__((optimize("-O0"))); clang (including Apple
# clang) only needs to ignore them.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wno-unknown-attributes -Wno-unknown-warning-option -Wno-c++20-extensions -Wno-c++20-designator)
endif()

# ---------------------------------------------------------------------------
# Resolve the firmware folder: <code>/src holds the firmware, <code>/libs the
# vendored libDaisy / DaisySP / coreJSON that CHOMPI Club modified.
# ---------------------------------------------------------------------------
if(IS_ABSOLUTE "${CHOMPI_FIRMWARE}")
  set(CHOMPI_FW_CODE "${CHOMPI_FIRMWARE}")
else()
  set(CHOMPI_FW_CODE "${CHOMPI_REPO_DIR}/firmware/chompi-${CHOMPI_FIRMWARE}/code")
endif()
set(CHOMPI_FW_SRC  "${CHOMPI_FW_CODE}/src")
set(CHOMPI_FW_LIBS "${CHOMPI_FW_CODE}/libs")
# DaisySP and coreJSON are built from the vendored copies. If the selected
# firmware's libs folder is not checked out, fall back to any other CHOMPI
# firmware's copy (the three firmwares vendor the same DaisySP / coreJSON).
if(NOT EXISTS "${CHOMPI_FW_LIBS}/DaisySP/Source/daisysp.h" OR NOT EXISTS "${CHOMPI_FW_LIBS}/coreJSON/source/core_json.c")
  file(GLOB _alt_libs "${CHOMPI_REPO_DIR}/firmware/chompi-*/code/libs")
  foreach(_l ${_alt_libs})
    if(EXISTS "${_l}/DaisySP/Source/daisysp.h" AND EXISTS "${_l}/coreJSON/source/core_json.c")
      message(STATUS "Using vendored DaisySP / coreJSON from ${_l}")
      set(CHOMPI_FW_LIBS "${_l}")
      break()
    endif()
  endforeach()
endif()

if(NOT CHOMPI_SIM_STUB)
  if(NOT EXISTS "${CHOMPI_FW_SRC}/chompi_main.cpp")
    message(FATAL_ERROR "Firmware sources not found at ${CHOMPI_FW_SRC}. Run scripts/fetch-firmware.sh or set CHOMPI_REPO_DIR / CHOMPI_FIRMWARE.")
  endif()

  # Stage a patched copy of the firmware sources (the originals stay untouched).
  set(CHOMPI_FW_STAGED "${CMAKE_BINARY_DIR}/firmware-staged")
  if(IS_ABSOLUTE "${CHOMPI_FIRMWARE}")
    set(CHOMPI_FW_PATCHES "${CMAKE_SOURCE_DIR}/firmware/patches/custom")
  else()
    set(CHOMPI_FW_PATCHES "${CMAKE_SOURCE_DIR}/firmware/patches/${CHOMPI_FIRMWARE}")
  endif()
  execute_process(
    COMMAND "${CMAKE_SOURCE_DIR}/scripts/stage-firmware.sh" "${CHOMPI_FW_SRC}" "${CHOMPI_FW_STAGED}" "${CHOMPI_FW_PATCHES}"
    RESULT_VARIABLE _stage_rc OUTPUT_VARIABLE _stage_out ERROR_VARIABLE _stage_err)
  if(NOT _stage_rc EQUAL 0)
    message(FATAL_ERROR "Staging the firmware failed:\n${_stage_out}\n${_stage_err}")
  endif()
  message(STATUS "Firmware staged at ${CHOMPI_FW_STAGED}\n${_stage_out}")

  # ---- DaisySP (portable DSP, built from the vendored copy) ----
  file(GLOB DAISYSP_SOURCES CONFIGURE_DEPENDS "${CHOMPI_FW_LIBS}/DaisySP/Source/*/*.cpp")
  add_library(daisysp STATIC ${DAISYSP_SOURCES})
  file(GLOB DAISYSP_MODULE_DIRS LIST_DIRECTORIES true "${CHOMPI_FW_LIBS}/DaisySP/Source/*")
  list(FILTER DAISYSP_MODULE_DIRS EXCLUDE REGEX "\\.(h|cpp)$")
  target_include_directories(daisysp PUBLIC "${CHOMPI_FW_LIBS}/DaisySP/Source" ${DAISYSP_MODULE_DIRS} "${CMAKE_SOURCE_DIR}/host/compat")
  target_compile_options(daisysp PRIVATE -w "SHELL:-include ${CMAKE_SOURCE_DIR}/host/compat/chompi_sim_prelude.h")

  # ---- coreJSON ----
  add_library(corejson STATIC "${CHOMPI_FW_LIBS}/coreJSON/source/core_json.c")
  target_include_directories(corejson PUBLIC "${CHOMPI_FW_LIBS}/coreJSON/source/include")

  # ---- libDaisy host shim + simulator core + firmware, one library ----
  file(GLOB SHIM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/host/libdaisy-sim/src/*.cpp")
  file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/sim/src/*.cpp")
  set(FW_SOURCES
    "${CHOMPI_FW_STAGED}/chompi_main.cpp"
    "${CHOMPI_FW_STAGED}/encoder.cpp"
    "${CHOMPI_FW_STAGED}/FileStreamingManager.cpp")
  file(GLOB FW_EXTRA_SOURCES CONFIGURE_DEPENDS "${CHOMPI_FW_STAGED}/*.cpp")
  list(REMOVE_ITEM FW_EXTRA_SOURCES ${FW_SOURCES})
  list(APPEND FW_SOURCES ${FW_EXTRA_SOURCES})

  add_library(chompi_sim STATIC ${SHIM_SOURCES} ${CORE_SOURCES} ${FW_SOURCES})
  target_include_directories(chompi_sim
    PUBLIC  "${CMAKE_SOURCE_DIR}/sim/include"
    PRIVATE "${CMAKE_SOURCE_DIR}/host/libdaisy-sim/include" "${CMAKE_SOURCE_DIR}/sim/src" "${CHOMPI_FW_STAGED}")
  target_compile_definitions(chompi_sim PRIVATE CHOMPI_SIM=1 CHOMPI_SIM_FIRMWARE="${CHOMPI_FIRMWARE}")
  target_link_libraries(chompi_sim PUBLIC daisysp corejson Threads::Threads)
  # The firmware is written for arm-none-eabi-gcc; keep the host build quiet about its style.
  set_source_files_properties(${FW_SOURCES} PROPERTIES COMPILE_OPTIONS
    "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-format;-Wno-sign-compare;-Wno-unused-function;-Wno-missing-field-initializers;-Wno-attributes;-Wno-unknown-attributes;-Wno-pedantic;-Wno-reorder;-Wno-unused-value;-Wno-parentheses;-Wno-misleading-indentation;-Wno-narrowing;-Wno-write-strings")
  target_compile_options(chompi_sim PRIVATE -fno-strict-aliasing "SHELL:-include ${CMAKE_SOURCE_DIR}/host/compat/chompi_sim_prelude.h")

  # On macOS the low 4 GB of the address space is reserved (__PAGEZERO); shrink
  # it so the simulator can map the Daisy's SDRAM window at 0xC0000000.
  if(APPLE)
    target_link_options(chompi_sim INTERFACE "-Wl,-pagezero_size,0x10000")
  endif()

  # ---- headless runner ----
  add_executable(chompi-sim frontends/headless/main.cpp)
  target_link_libraries(chompi-sim PRIVATE chompi_sim)
endif()

# ---- SDL2 GUI ----
if(CHOMPI_SIM_BUILD_GUI)
  find_package(SDL2 QUIET)
  if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
    endif()
  endif()
  if(SDL2_FOUND)
    file(GLOB GUI_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/frontends/sdl/*.cpp")
    list(FILTER GUI_SOURCES EXCLUDE REGEX "sim_stub\\.cpp$")
    add_executable(chompi-sim-gui ${GUI_SOURCES})
    if(CHOMPI_SIM_STUB)
      target_sources(chompi-sim-gui PRIVATE frontends/sdl/sim_stub.cpp)
      target_include_directories(chompi-sim-gui PRIVATE "${CMAKE_SOURCE_DIR}/sim/include")
      target_link_libraries(chompi-sim-gui PRIVATE Threads::Threads)
    else()
      target_link_libraries(chompi-sim-gui PRIVATE chompi_sim)
    endif()
    if(TARGET SDL2::SDL2)
      target_link_libraries(chompi-sim-gui PRIVATE SDL2::SDL2)
    elseif(TARGET PkgConfig::SDL2)
      target_link_libraries(chompi-sim-gui PRIVATE PkgConfig::SDL2)
    else()
      target_include_directories(chompi-sim-gui PRIVATE ${SDL2_INCLUDE_DIRS})
      target_link_libraries(chompi-sim-gui PRIVATE ${SDL2_LIBRARIES})
    endif()
  else()
    message(WARNING "SDL2 not found: the GUI front-end will not be built (headless runner is unaffected).")
  endif()
endif()

# ---- self-test of the device models (no UI) ----
if(NOT CHOMPI_SIM_STUB)
  add_executable(chompi-sim-selftest tests/selftest.cpp)
  target_include_directories(chompi-sim-selftest PRIVATE "${CMAKE_SOURCE_DIR}/host/libdaisy-sim/include" "${CHOMPI_FW_STAGED}")
  target_compile_definitions(chompi-sim-selftest PRIVATE CHOMPI_SIM=1)
  target_link_libraries(chompi-sim-selftest PRIVATE chompi_sim)
  target_compile_options(chompi-sim-selftest PRIVATE -Wno-attributes)
endif()
