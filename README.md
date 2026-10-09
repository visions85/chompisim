# chompi-sim: a desktop simulator for the CHOMPI sampler

Runs the real, unmodified CHOMPI firmware (TAPE / TEMPO / WAVE and community
builds based on them) on a laptop, with the keys, knobs, LEDs, SD card, MIDI and
battery charger simulated. Press keys on your computer keyboard, turn knobs with
the mouse wheel, hear the output through your sound card, and watch the 35 RGB
LEDs do what they do on the instrument.

The firmware is compiled from source against a host implementation of libDaisy
(`host/libdaisy-sim`). Nothing in the firmware is rewritten: the only change is
a 30-line `#ifdef CHOMPI_SIM` patch to the firmware's `chompi_main.cpp` that
renames `main()` and lets the main loop exit.

Status: all three factory firmwares boot from their factory cards and play.

| Firmware | Boots | Keybed, LEDs, MIDI out | Audio | Notes |
|---|---|---|---|---|
| WAVE 1.0 | yes | yes | yes | presets and options are saved to the card folder; MIDI in works |
| TAPE 2.0 | yes | yes | yes | samples stream from the card folder (168-file factory card) |
| TEMPO 1.0 | yes | yes | yes | arpeggiator transport and MIDI clock out work; needs the two small patches in `firmware/patches/tempo` |

Both run modes (realtime and deterministic lockstep) produce the same output.

## Quick start

Requirements: CMake 3.16+, a C++17 compiler (GCC 10+ or clang 12+), git, and
SDL2 development headers for the GUI. The headless runner needs no SDL.

```sh
# macOS: Xcode command line tools, then Homebrew
xcode-select --install
brew install cmake sdl2
# Debian / Ubuntu
sudo apt install git cmake g++ libsdl2-dev
# Fedora
sudo dnf install git cmake gcc-c++ SDL2-devel
```

```sh
scripts/fetch-firmware.sh wave        # sparse clone of CHOMPI-Club/CHOMPI into third_party/
cmake -S . -B build
cmake --build build -j

# a folder plays the role of the microSD card; use a COPY of the factory card,
# the firmware writes options.json / presets.json to it
cp -R third_party/CHOMPI/firmware/card-profiles/wave-1.0 card

./build/chompi-sim-gui --card card          # the instrument in a window
./build/chompi-sim --card card --seconds 12 --script examples/phrase.txt --wav out.wav
```

For TAPE or TEMPO, fetch and build with the firmware name:

```sh
scripts/fetch-firmware.sh tape tempo
cmake -S . -B build-tape -DCHOMPI_FIRMWARE=tape && cmake --build build-tape -j
cp -R third_party/CHOMPI/firmware/card-profiles/tape-2.0 card-tape
./build-tape/chompi-sim-gui --card card-tape
```

Boot takes about seven seconds, like the hardware: boot animation, the rainbow
wave, then the normal page. Keys do nothing until the rainbow has finished.

### macOS notes

The build is the same on macOS (Intel or Apple Silicon); CMake finds
Homebrew's SDL2 on its own. Audio goes through SDL's CoreAudio backend;
nothing is recorded, so macOS will not ask for microphone access.

The Daisy's SDRAM bank is not mapped at its hardware address on macOS (the
low 4 GB of a process are reserved there, and arm64 executables cannot change
that). Firmware that keeps the bank's base address in a variable, as TEMPO
does, is patched to take the simulator's 64 MB stand-in block instead, so all
three firmwares run the same on both systems.

## The window

The layout follows the Rev4 board file: every key socket, encoder, LED and
the mode switch is drawn where the board puts it.

```
  o        o        o        o        o      o   (  )   o   o        o       <- panel LEDs
 mode  [CHOMPI]  (PITCH)   (A)      (B)      (C)   (TRANSPORT)  [PLAY] [LOOP]  (VOLUME)
       [s] [d]      [g] [h] [j]      [2] [3]      [5] [6] [7]                  <- upper row of caps
     [z] [x] [c] [v] [b] [n] [m] [q] [w] [e] [r] [t] [y] [u] [i]               <- lower row of caps
```

Each key cap is lit by the LED under it. No artwork or logos are reproduced.

| Control | Computer |
|---|---|
| Piano keys, lower octave (C to B) | `z s x d c v g b h n j m` |
| Piano keys, upper octave (C to C) | `q 2 w 3 e r 5 t 6 y 7 u i` |
| PLAY / LOOP / CHOMPI | Space / Return / Left Shift (hold) |
| Mode toggle switch | Tab (latches), or click it |
| Any key or button | click it with the mouse |
| Knobs | drag up or down on the knob, or scroll over it (mouse wheel or trackpad); a click pushes the encoder, the right button holds it down |
| Transport knob / volume knob | `[` `]` / `-` `=` |
| Last touched small knob | Left / Right arrows |
| Push encoders SW1 to SW6 | F1 to F6 |
| Quit | Escape |

The knobs do what the firmware makes them do. In WAVE the PITCH knob is a fine
tune: 0.003 of its range per detent, roughly 14 detents per semitone, one
octave each way over the whole range, and it retunes notes that are already
sounding. A click on it switches that knob to wavetable cycling (its LED
changes colour) and a second click brings pitch back. Octave shifts live in
the menu: with the mode switch down (`Tab`), hold the CHOMPI key (`Left
Shift`) and press the first black key (`s`) for an octave down or the second
(`d`) for an octave up.

Command line: `--card DIR` (default `card`), `--no-audio` (run without a sound
card), `--pair 0|1` (send the headphone or the line output to the sound card,
default line), `--scale F`, `--screenshot FILE.bmp`, `--exit-after SECONDS`.

The output of the firmware is quiet at the WAVE defaults (the final compressor
is a menu-page setting); turn the volume knob up or use the firmware's menu, as
on the instrument.

## Headless runner

`chompi-sim` runs the firmware in lockstep with the audio clock: every run is
deterministic and as fast as the machine can render. It plays a script and
writes a WAV file, LED snapshots and a picture of the panel.

```
8.0  note 12 1.0        # press piano semitone 12 (0 = low C) for one second
9.0  press PLAY 0.1     # buttons: PLAY LOOP CHOMPI KEY_27 ENC_1_SW ... or a Button id
9.5  button LOOP down   # raw state
10.0 enc 4 -3           # physical encoder 0..5 (4 = transport), detents, + = clockwise
10.5 encpress 5 0.1     # push the volume knob
11.0 toggle down        # mode switch
11.5 midi 90 3C 7F      # bytes to the TRS MIDI input, hex
12.0 usbmidi B0 14 40   # bytes to the USB MIDI input
12.5 leds               # append an LED snapshot to --leds FILE
13.0 card out           # pull the SD card (card in puts it back)
13.5 power off          # unplug USB power; battery low|ok sets the charger's battery flag
```

```
chompi-sim --card DIR [--seconds N] [--script FILE] [--wav FILE] [--pair 0|1]
           [--leds FILE] [--ppm FILE] [--midi-out FILE] [--quiet] [--realtime [--burst N]] [--trace]
```

`--trace` prints a line whenever the LED state or the MIDI output changes, with
the time; it is the quickest way to see what a firmware does in response to a
script. `--realtime` runs the same script at wall-clock pace in the
free-running thread mode the GUI uses, and `--burst N` renders N blocks back to
back before sleeping, the way a sound card buffer does (a 256-frame buffer is
eleven blocks). That is how to reproduce a timing problem seen in the GUI from
a script.

`scripts/smoke-test.sh` builds everything, runs the device self-test, boots the
WAVE factory card, plays a note from the keybed and one over MIDI and checks
the audio, the MIDI output and the key LED. `docs/demo-wave-factory-card.wav`
was rendered this way from `examples/demo.txt` (normalised afterwards).
`docs/gui-wave.png` and `docs/gui-tape.png` show the window with the real
firmwares holding a C major chord; `docs/gui-stub.png` is the same window on
the stand-in core used to develop the front-end.

## How it works

```
frontends/sdl, frontends/headless      windows, sound card, scripts
            │  chompi_sim::Sim (sim/include/chompi_sim/sim.h)
            ▼
sim/src/device.*                       pins, CD4021 chains, encoders, timers, DMA,
                                       audio clock, LED decoding, MIDI ports, MP2722,
                                       lockstep scheduler, firmware thread
            ▲  libDaisy API (same headers and class names as libDaisy)
host/libdaisy-sim                      GPIO, TimerHandle, TimChannel, I2CHandle, SaiHandle,
                                       UartHandler, AudioHandle, DaisySeed, System, FatFs...
            ▲
build/firmware-staged                  the firmware, copied and patched at configure time
third_party/CHOMPI/.../libs            DaisySP and coreJSON, built from the vendored copies
```

What is simulated, and how faithfully:

- **Keys, encoders, toggle.** The firmware reads its 40 buttons through five
  CD4021 shift registers and four encoders through a sixth. The simulator models
  the chips bit by bit under the real libDaisy driver, including its debouncing.
  Encoder turns are played out as quadrature phases slow enough for the
  firmware's 1 kHz debouncer. The big knob (SW5) and the volume knob (SW6) read
  their A/B and push lines directly from GPIO, as on the board.
- **LEDs.** CHOMPI bit-bangs its two WS2812 chains with timer PWM plus DMA. The
  simulator decodes the pulse-width DMA buffer back into colours and fires the
  end-of-transfer callback after the time the wire transfer takes, so the
  firmware's chain ping-pong runs at hardware speed. Colours are rescaled to
  display brightness (the firmware dims them by 1/4 and 1/11 for the real LEDs).
- **Audio.** Two codecs, four channels each way, 24-sample blocks at 48 kHz,
  exactly the hardware layout (0/1 headphones, 2/3 line out; inputs 0 mic, 2/3
  aux). The sound card gets one stereo pair; inputs are silent for now.
- **Time.** `System::GetNow()` follows the audio sample clock in both modes.
  The firmware polls its keys and encoders from the audio callback, and a sound
  card renders blocks in bursts, so a wall clock would let several encoder
  phases slip past one debouncer sample and knobs would lag or skip steps.
  Realtime mode lets the firmware's main loop run freely, as it does on the
  MCU, with its delays sleeping on the wall clock. Lockstep mode hands a baton
  between the audio thread and the firmware thread at every delay call, so a
  scripted run is reproducible bit for bit.
- **Timers.** TIM4 (SD card servicing) and TIM16 (MIDI clock, retuned on the
  fly by the firmware's clock manager) fire from the scheduler at
  240 MHz / ((PSC+1)(ARR+1)), as on the STM32H750.
- **SD card.** FatFs calls map to a folder. Names resolve case-insensitively
  like FAT; directory listings are sorted. The card can be "removed" through
  `SetCardPresent` to exercise the no-card page.
- **MIDI.** TRS MIDI out goes through the firmware's DMA path with the end
  callback timed at 31250 baud; USB MIDI is a second port. Both inputs are fed
  from the API.
- **Battery charger.** The MP2722 registers the firmware polls report USB
  power present, battery full; the API can unplug it or make the battery low.
- **Not simulated.** The USB mass-storage and bootloader paths (there is no
  bootloader: the firmware starts directly), QSPI flash, the microphone and aux
  inputs (silent), the analog output stage and its gain.
- **Interrupt priorities.** Everything the hardware does in an interrupt runs
  on the one audio thread. When the firmware busy-waits inside an interrupt
  (the three-second no-card animation lives in the SD card timer), the LEDs
  keep updating but audio is not rendered for that time: in realtime mode the
  sound card stalls, in lockstep mode the firmware clock jumps ahead of the
  script clock.

## Adding a firmware

`-DCHOMPI_FIRMWARE=tape` (or `tempo`, or an absolute path to a `<firmware>/code`
folder for a community build such as POLY) picks the sources. At configure
time the sources are copied to `build/firmware-staged` and the patches in
`firmware/patches/<name>/` (`custom/` for an absolute path) are applied.

`scripts/make-sim-patch.py path/to/chompi_main.cpp out.patch` generates the
entry-point patch for any CHOMPI-derived firmware: it includes the hooks
header, renames `main()` to `chompi_firmware_main()`, makes the main loop stop
on `chompi_sim_running()`, turns explicit SDRAM/DTCM section attributes into
libDaisy's macros (which the simulator defines as empty) and makes
`ZeroSDRAM()` a no-op. Everything it adds is inside `#ifdef CHOMPI_SIM`, so the
patched file still builds for the hardware.

Two more things came up with the factory firmwares and may apply to forks:

- TAPE calls `DelayLine::SetDelay(10u)`, which is ambiguous on a 64-bit host
  (`size_t` vs `float`); `firmware/patches/tape/warble_setdelay.patch` adds a
  cast. One include spells `Limiter.h` for `limiter.h`; the staging script adds
  symlinks for such case differences so Linux builds work.
- TEMPO indexes its arpeggiator note lists while they are empty (reads address
  0, which the MCU tolerates and a desktop does not).
  `firmware/patches/tempo/arp_empty_notes.patch` reserves storage up front and
  reorders one condition. TEMPO's sample manager addresses SDRAM through a
  base-address variable initialised to `0xC0000000`; the entry-point patch
  points it at the simulator's 64 MB stand-in block (`chompi_sim_sdram()`),
  which on Linux usually sits at that very address and on macOS cannot.

If a firmware uses a libDaisy class the shim does not have yet, add it under
`host/libdaisy-sim` mirroring the real header; the shim already covers
everything the three factory firmwares touch.

## Testing

```sh
./build/chompi-sim-selftest card     # FatFs, button chain, encoders, charger model
scripts/smoke-test.sh                # full boot + note + MIDI + LED check
```

## License

MIT. See `THIRD_PARTY.md` for the libDaisy files copied into the shim and the
CHOMPI sources this builds on. Not affiliated with CHOMPI Club or Chase Bliss;
no CHOMPI artwork or binaries are included.
