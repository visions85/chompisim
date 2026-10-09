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
set(CHOMPI_FIRMWARES "" CACHE STRING "Firmwares to build, semicolon separated (wave;tape;tempo). Empty: every one whose sources are in CHOMPI_REPO_DIR")
set(CHOMPI_FIRMWARE "" CACHE STRING "One firmware: wave, tape, tempo, or an absolute path to a <firmware>/code folder (overrides CHOMPI_FIRMWARES)")
set(CHOMPI_REPO_DIR "${CMAKE_SOURCE_DIR}/third_party/CHOMPI" CACHE PATH "Checkout of github.com/CHOMPI-Club/CHOMPI (see scripts/fetch-firmware.sh)")

find_package(Threads REQUIRED)

# The firmware and the libDaisy headers copied into the shim carry GCC-only
# attributes such as __attribute__((optimize("-O0"))); clang (including Apple
# clang) only needs to ignore them.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wno-unknown-attributes -Wno-unknown-warning-option -Wno-c++20-extensions -Wno-c++20-designator)
endif()

set(CHOMPI_PRELUDE "SHELL:-include ${CMAKE_SOURCE_DIR}/host/compat/chompi_sim_prelude.h")
set(CHOMPI_FW_WARNINGS
  "-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-format;-Wno-sign-compare;-Wno-unused-function;-Wno-missing-field-initializers;-Wno-attributes;-Wno-unknown-attributes;-Wno-pedantic;-Wno-reorder;-Wno-unused-value;-Wno-parentheses;-Wno-misleading-indentation;-Wno-narrowing;-Wno-write-strings")

# ---------------------------------------------------------------------------
# Which firmwares. Each has <code>/src with the firmware and <code>/libs with
# the vendored libDaisy / DaisySP / coreJSON that CHOMPI Club modified.
# ---------------------------------------------------------------------------
set(CHOMPI_FW_NAMES "")
if(CHOMPI_FIRMWARE)
  set(_fw_entries "${CHOMPI_FIRMWARE}")
elseif(CHOMPI_FIRMWARES)
  set(_fw_entries "${CHOMPI_FIRMWARES}")
else()
  set(_fw_entries "")
  foreach(_fw wave tape tempo)
    if(EXISTS "${CHOMPI_REPO_DIR}/firmware/chompi-${_fw}/code/src/chompi_main.cpp")
      list(APPEND _fw_entries ${_fw})
    endif()
  endforeach()
endif()
foreach(_entry ${_fw_entries})
  if(IS_ABSOLUTE "${_entry}")
    set(_name "custom")
    set(_code "${_entry}")
  else()
    string(TOLOWER "${_entry}" _name)
    set(_code "${CHOMPI_REPO_DIR}/firmware/chompi-${_name}/code")
  endif()
  list(APPEND CHOMPI_FW_NAMES ${_name})
  set(CHOMPI_FW_CODE_${_name} "${_code}")
endforeach()

if(NOT CHOMPI_SIM_STUB AND NOT CHOMPI_FW_NAMES)
  message(FATAL_ERROR "No firmware sources found under ${CHOMPI_REPO_DIR}/firmware. Run scripts/fetch-firmware.sh or set CHOMPI_REPO_DIR / CHOMPI_FIRMWARES.")
endif()

# DaisySP and coreJSON are built once from the vendored copies (the three
# firmwares vendor the same ones); any checked-out libs folder will do.
set(CHOMPI_FW_LIBS "")
foreach(_name ${CHOMPI_FW_NAMES})
  if(EXISTS "${CHOMPI_FW_CODE_${_name}}/libs/DaisySP/Source/daisysp.h" AND EXISTS "${CHOMPI_FW_CODE_${_name}}/libs/coreJSON/source/core_json.c")
    set(CHOMPI_FW_LIBS "${CHOMPI_FW_CODE_${_name}}/libs")
    break()
  endif()
endforeach()
if(NOT CHOMPI_FW_LIBS)
  file(GLOB _alt_libs "${CHOMPI_REPO_DIR}/firmware/chompi-*/code/libs")
  foreach(_l ${_alt_libs})
    if(EXISTS "${_l}/DaisySP/Source/daisysp.h" AND EXISTS "${_l}/coreJSON/source/core_json.c")
      set(CHOMPI_FW_LIBS "${_l}")
      break()
    endif()
  endforeach()
endif()

if(NOT CHOMPI_SIM_STUB)
  if(NOT CHOMPI_FW_LIBS)
    message(FATAL_ERROR "Vendored DaisySP / coreJSON not found under ${CHOMPI_REPO_DIR}/firmware/chompi-*/code/libs. Run scripts/fetch-firmware.sh.")
  endif()
  message(STATUS "Firmwares: ${CHOMPI_FW_NAMES}; DaisySP / coreJSON from ${CHOMPI_FW_LIBS}")

  # ---- DaisySP (portable DSP, built from the vendored copy) ----
  file(GLOB DAISYSP_SOURCES CONFIGURE_DEPENDS "${CHOMPI_FW_LIBS}/DaisySP/Source/*/*.cpp")
  add_library(daisysp STATIC ${DAISYSP_SOURCES})
  file(GLOB DAISYSP_MODULE_DIRS LIST_DIRECTORIES true "${CHOMPI_FW_LIBS}/DaisySP/Source/*")
  list(FILTER DAISYSP_MODULE_DIRS EXCLUDE REGEX "\\.(h|cpp)$")
  target_include_directories(daisysp PUBLIC "${CHOMPI_FW_LIBS}/DaisySP/Source" ${DAISYSP_MODULE_DIRS} "${CMAKE_SOURCE_DIR}/host/compat")
  target_compile_options(daisysp PRIVATE -w "${CHOMPI_PRELUDE}")

  # ---- coreJSON ----
  add_library(corejson STATIC "${CHOMPI_FW_LIBS}/coreJSON/source/core_json.c")
  target_include_directories(corejson PUBLIC "${CHOMPI_FW_LIBS}/coreJSON/source/include")

  # ---- libDaisy host shim + simulator core: one library, firmware-independent ----
  file(GLOB SHIM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/host/libdaisy-sim/src/*.cpp")
  file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/sim/src/*.cpp")
  add_library(chompi_core STATIC ${SHIM_SOURCES} ${CORE_SOURCES})
  target_include_directories(chompi_core
    PUBLIC  "${CMAKE_SOURCE_DIR}/sim/include" "${CMAKE_SOURCE_DIR}/host/libdaisy-sim/include"
    PRIVATE "${CMAKE_SOURCE_DIR}/sim/src")
  target_compile_definitions(chompi_core PUBLIC CHOMPI_SIM=1)
  target_compile_options(chompi_core PRIVATE -fno-strict-aliasing "${CHOMPI_PRELUDE}")
  target_link_libraries(chompi_core PUBLIC daisysp corejson Threads::Threads)

  # ---- one object library per firmware: a patched copy of its sources ----
  foreach(_name ${CHOMPI_FW_NAMES})
    set(_src "${CHOMPI_FW_CODE_${_name}}/src")
    if(NOT EXISTS "${_src}/chompi_main.cpp")
      message(FATAL_ERROR "Firmware sources not found at ${_src}. Run scripts/fetch-firmware.sh ${_name} or set CHOMPI_REPO_DIR / CHOMPI_FIRMWARES.")
    endif()
    set(_staged "${CMAKE_BINARY_DIR}/firmware-staged-${_name}")
    set(CHOMPI_FW_STAGED_${_name} "${_staged}")
    execute_process(
      COMMAND "${CMAKE_SOURCE_DIR}/scripts/stage-firmware.sh" "${_src}" "${_staged}" "${CMAKE_SOURCE_DIR}/firmware/patches/${_name}"
      RESULT_VARIABLE _stage_rc OUTPUT_VARIABLE _stage_out ERROR_VARIABLE _stage_err)
    if(NOT _stage_rc EQUAL 0)
      message(FATAL_ERROR "Staging the ${_name} firmware failed:\n${_stage_out}\n${_stage_err}")
    endif()
    message(STATUS "${_name} firmware staged at ${_staged}\n${_stage_out}")

    file(GLOB _fw_sources CONFIGURE_DEPENDS "${_staged}/*.cpp")
    add_library(chompi_fw_${_name} OBJECT ${_fw_sources})
    target_include_directories(chompi_fw_${_name} PRIVATE "${_staged}")
    target_link_libraries(chompi_fw_${_name} PRIVATE chompi_core) # headers and defines only; objects are linked by the executables
    # The firmware is written for arm-none-eabi-gcc; keep the host build quiet about its style.
    target_compile_options(chompi_fw_${_name} PRIVATE -fno-strict-aliasing "${CHOMPI_PRELUDE}" ${CHOMPI_FW_WARNINGS})
  endforeach()

  # ---- headless runner, one per firmware, and the launcher that picks one ----
  foreach(_name ${CHOMPI_FW_NAMES})
    add_executable(chompi-sim-${_name} frontends/headless/main.cpp $<TARGET_OBJECTS:chompi_fw_${_name}>)
    target_link_libraries(chompi-sim-${_name} PRIVATE chompi_core)
    target_compile_definitions(chompi-sim-${_name} PRIVATE CHOMPI_SIM_FIRMWARE="${_name}")
  endforeach()
  add_executable(chompi-sim frontends/launcher/main.cpp)
  target_include_directories(chompi-sim PRIVATE "${CMAKE_SOURCE_DIR}/frontends/common")
  target_compile_definitions(chompi-sim PRIVATE LAUNCHER_BASE="chompi-sim")
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
    function(chompi_link_sdl target)
      if(TARGET SDL2::SDL2)
        target_link_libraries(${target} PRIVATE SDL2::SDL2)
      elseif(TARGET PkgConfig::SDL2)
        target_link_libraries(${target} PRIVATE PkgConfig::SDL2)
      else()
        target_include_directories(${target} PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(${target} PRIVATE ${SDL2_LIBRARIES})
      endif()
      target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/frontends/common")
    endfunction()
    if(CHOMPI_SIM_STUB)
      add_executable(chompi-sim-gui ${GUI_SOURCES} frontends/sdl/sim_stub.cpp)
      target_include_directories(chompi-sim-gui PRIVATE "${CMAKE_SOURCE_DIR}/sim/include")
      target_compile_definitions(chompi-sim-gui PRIVATE CHOMPI_SIM_FIRMWARE="stub")
      target_link_libraries(chompi-sim-gui PRIVATE Threads::Threads)
      chompi_link_sdl(chompi-sim-gui)
    else()
      foreach(_name ${CHOMPI_FW_NAMES})
        add_executable(chompi-sim-gui-${_name} ${GUI_SOURCES} $<TARGET_OBJECTS:chompi_fw_${_name}>)
        target_link_libraries(chompi-sim-gui-${_name} PRIVATE chompi_core)
        target_compile_definitions(chompi-sim-gui-${_name} PRIVATE CHOMPI_SIM_FIRMWARE="${_name}")
        chompi_link_sdl(chompi-sim-gui-${_name})
      endforeach()
      add_executable(chompi-sim-gui frontends/launcher/main.cpp)
      target_include_directories(chompi-sim-gui PRIVATE "${CMAKE_SOURCE_DIR}/frontends/common")
      target_compile_definitions(chompi-sim-gui PRIVATE LAUNCHER_BASE="chompi-sim-gui")
    endif()
  else()
    message(WARNING "SDL2 not found: the GUI front-end will not be built (headless runner is unaffected).")
  endif()
endif()

# ---- self-test of the device models (no UI), against the first firmware (WAVE when built) ----
if(NOT CHOMPI_SIM_STUB)
  if("wave" IN_LIST CHOMPI_FW_NAMES)
    set(_test_fw wave)
  else()
    list(GET CHOMPI_FW_NAMES 0 _test_fw)
  endif()
  add_executable(chompi-sim-selftest tests/selftest.cpp $<TARGET_OBJECTS:chompi_fw_${_test_fw}>)
  target_include_directories(chompi-sim-selftest PRIVATE "${CHOMPI_FW_STAGED_${_test_fw}}" "${CMAKE_SOURCE_DIR}/sim/src")
  target_link_libraries(chompi-sim-selftest PRIVATE chompi_core)
  target_compile_options(chompi-sim-selftest PRIVATE -Wno-attributes)
endif()
