# Third-party code in chompi-sim

| What | Where | Origin | License |
|---|---|---|---|
| Portable libDaisy headers and sources copied verbatim: `util/FIFO.h`, `util/Stack.h`, `util/ringbuffer.h`, `util/color.h` + `color.cpp`, `util/wav_format.h`, `hid/MidiEvent.h`, `hid/midi_parser.h` + `.cpp`, `hid/midi.h`, `hid/switch.h` + `.cpp`, `dev/sr_4021.h`, `ui/UI.h` + `UI.cpp`, `ui/UiEventQueue.h`, `ui/PotMonitor.h` | `host/libdaisy-sim/` | [libDaisy](https://github.com/electro-smith/libDaisy) by Electrosmith, as vendored in the CHOMPI repository | MIT |
| `daisy_core.h` (copied, memory-section macros neutralised, interrupt intrinsics added) | `host/libdaisy-sim/include/daisy_core.h` | libDaisy | MIT |
| Every other file under `host/libdaisy-sim/` re-implements the libDaisy API for the host. Class and method names follow libDaisy so the firmware compiles unchanged. | `host/libdaisy-sim/` | written for chompi-sim | MIT |
| FatFs public API (`ff.h`, `diskio.h`, `ffconf.h`, `integer.h`) re-implemented over the host file system | `host/libdaisy-sim/include/`, `src/fatfs_posix.cpp` | interface by ChaN (FatFs R0.12c); implementation written for chompi-sim | FatFs license (BSD-style) for the interface, MIT for the implementation |
| CHOMPI firmware, vendored libDaisy, DaisySP and coreJSON | fetched into `third_party/CHOMPI` by `scripts/fetch-firmware.sh`, never copied into this tree | [CHOMPI-Club/CHOMPI](https://github.com/CHOMPI-Club/CHOMPI) | MIT (code); the CHOMPI name and marks are excluded, see that repository's `TRADEMARKS.md` |
| `firmware/patches/*` | this tree | small `#ifdef CHOMPI_SIM` additions to CHOMPI's `chompi_main.cpp` | MIT |
| SDL2 (GUI front-end only, linked, not included) | system package | libsdl.org | zlib |

This project is not affiliated with CHOMPI Club or Chase Bliss. It does not
contain CHOMPI artwork, logos or firmware binaries.
