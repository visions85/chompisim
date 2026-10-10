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
| TEMPO 1.0 | yes | yes | yes | arpeggiator transport and MIDI clock out work; needs the three small patches in `firmware/patches/tempo` |
| GRAIN 0.1 | yes | yes | yes | a granular sampler written for this project, in `firmware/grain`; see below |

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
scripts/fetch-firmware.sh             # sparse clone of CHOMPI-Club/CHOMPI into third_party/ (all three firmwares)
cmake -S . -B build
cmake --build build -j                # every firmware found: chompi-sim-gui-wave, -tape, -tempo and the launchers

# folders play the role of the microSD card; use COPIES of the factory cards,
# the firmware writes options.json / presets.json to them
cp -R third_party/CHOMPI/firmware/card-profiles cards

./build/chompi-sim-gui --cards cards        # the instrument in a window; the bar switches firmware
./build/chompi-sim-gui --card cards/wave-1.0 # one card: the firmware is read off the card
./build/chompi-sim --card cards/wave-1.0 --seconds 12 --script examples/phrase.txt --wav out.wav
```

`chompi-sim-gui` and `chompi-sim` are launchers: they pick the firmware from
`--firmware NAME`, else from the firmware binary on the `--card` folder, else
from the first firmware that has a card under `--cards DIR`, and run the
matching `chompi-sim[-gui]-<firmware>` next to them. The tabs in the bar above
the instrument reboot into another firmware the same way, with that firmware's
card from the `--cards` folder (`wave`, `tape`, `tempo` or `wave-1.0`...).
`scripts/fetch-firmware.sh wave` and `-DCHOMPI_FIRMWARES=wave` fetch and build
a single firmware.

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
       [w] [e]      [t] [y] [u]      [o] [p]      [ ] [ ] [ ]                  <- upper row of caps
     [a] [s] [d] [f] [g] [h] [j] [k] [l] [;] ['] [ ] [ ] [ ] [ ]               <- lower row of caps
```

The computer keyboard is laid out like a DAW's: the home row plays the white
keys (the lower row of caps) and the row above it the black keys, from the
low C to the F an octave and a fourth up. `x` moves that span up an octave to
reach the top caps and `z` brings it back; the key map overlay prints the
letters on the caps they play right now. Physical key positions are used, so
the rows hold on any keyboard layout.

Each key cap is lit by the LED under it. The bar above the instrument names
the firmware, in its colour, with tabs to switch, and the panel tells what the
controls do in that firmware: the function of each knob and what its click
pages hold (WAVE: attack, release, delay feedback...; TAPE: sample start and
end, verb and delay, tape speed...; TEMPO: grain, clock division...), what
CHOMPI, PLAY and LOOP do (record, looper, sequencer, pattern), and, when the
mode switch is down, the menu layer printed on the caps: the black keys' jobs
(save, copy, erase, banks, inputs, octave, gate...) and the preset or sample
slot each white key selects. A firmware with a sound list (GRAIN) gets a
SOUND menu in the bar: the card's sounds and the recording slot, with the
selected one as the firmware reports it; picking a row sends the choice to
the firmware over its MIDI input. A key map overlay, shown at start
and toggled with `/` or `?`, shades the instrument, prints each piano key's
letter on its cap and draws a description box with an arrow to every switch
and knob; the arrow-key note moves to whichever small knob was touched last.
No artwork or logos are reproduced.

| Control | Computer |
|---|---|
| White keys (lower row of caps), C D E F G A B C D E F | `a s d f g h j k l ; '` |
| Black keys (upper row), C# D# F# G# A# C# D# | `w e t y u o p` |
| Octave of the computer keyboard | `x` up (the top caps), `z` down |
| PLAY / LOOP / CHOMPI | Space / Return / Left Shift (hold) |
| Mode toggle switch | Tab (latches), or click it |
| Any key or button | click it with the mouse |
| Knobs | drag up or down on the knob, or scroll over it (mouse wheel or trackpad); a click pushes the encoder, the right button holds it down |
| Transport knob / volume knob | `[` `]` / `-` `=` |
| Last touched small knob | Left / Right arrows |
| Push encoders SW1 to SW6 | F1 to F6 |
| Key map overlay | `/` or `?` (toggle; `--no-keymap` starts without it) |
| Sound menu (GRAIN) | click SOUND in the bar, then a row; Escape or a click elsewhere closes it |
| Quit | Escape |

The knobs do what the firmware makes them do. In WAVE the PITCH knob is a fine
tune: 0.003 of its range per detent, roughly 14 detents per semitone, one
octave each way over the whole range, and it retunes notes that are already
sounding. A click on it switches that knob to wavetable cycling (its LED
changes colour) and a second click brings pitch back. Octave shifts live in
the menu: with the mode switch down (`Tab`), hold the CHOMPI key (`Left
Shift`) and press the first black key (`w`) for an octave down or the second
(`e`) for an octave up.

Command line: `--card DIR` (default `card`), `--cards DIR` (a folder of card
folders, one per firmware), `--firmware wave|tape|tempo|grain` (launcher), `--no-audio`
(run without a sound card), `--pair 0|1` (send the headphone or the line output
to the sound card, default line), `--scale F`, `--screenshot FILE.bmp`,
`--exit-after SECONDS`, `--no-keymap`.

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
8.6  input tone.wav     # play a WAV into the inputs (then: loop, a gain); "input stop" stops it
8.0  jack in            # a cable in the aux jack (in|out): the firmware records the aux input, not the mic
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
`docs/gui-wave.png` shows the window with the key map overlay and WAVE holding
a C major chord, `docs/gui-tape.png` the plain panel with TAPE holding the same
chord, `docs/gui-grain.png` GRAIN with its playheads on the keys, and
`docs/gui-stub.png` the stand-in core used to develop the front-end.

## Sampling: sounds into the inputs

The firmwares record from the built-in microphone or, with a cable in the aux
jack, from the aux input. The simulator feeds those inputs from a sound file,
or from the computer's microphone, so you can sample into the simulated
instrument the way you would into the real one.

- The INPUT section of the bar has it all: `LOAD` opens a file dialog (the
  system one on macOS, zenity or kdialog on Linux) and plays the chosen WAV
  into the inputs, `PLAY` / `STOP` replay or stop it, `MIC` switches the
  computer's microphone (the default recording device) on and off, `JACK`
  flips the aux jack, and the meter shows what reaches the inputs. The keys
  are `F10` load, `F7` play, `F8` stop, `F9` microphone. Dropping a WAV onto
  the window loads and plays it too.
- From the command line, `--input sound.wav` loads a sound at start (PCM or
  float, any rate, mono or stereo; it is resampled to 48 kHz), `--loop` and
  `--gain` adjust it, `--mic` opens the microphone.
- A loaded sound plugs the aux jack, so the firmware takes it in stereo
  through its line-in path; `--mic-in` leaves the jack empty and the sound
  arrives as a mono microphone signal instead (`--line-in` forces the jack).
- macOS asks for microphone access the first time, for the terminal the
  simulator was started from; if the meter stays flat with `MIC` on, allow it
  under System Settings, Privacy & Security, Microphone.
- The status line shows the loaded sound, its play position and the jack.

In TAPE, with the mode switch up, the CHOMPI key records while it is held
(or latches, if that option is on): hold it, press `PLAY` in the bar (or
`F7`), or speak with `MIC` on, release it, and the keys play the new sample
from the chompi slot. The save is in the menu: mode
switch down, hold CHOMPI, press the top black key (`x` then `u`: the keyboard's upper octave) for SAVE, release
CHOMPI, press the white key of the slot, press CHOMPI; the card folder gets
`jammi_a<slot>.wav`. `examples/record-tape.txt` scripts exactly that with
`examples/tone.wav` and plays the saved slot at the end; TEMPO records the
same way. WAVE does not use the inputs.

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

## GRAIN: a granular sampler firmware

`firmware/grain` is a fourth firmware, grown from WAVE: the keybed, LEDs, MIDI,
presets, menu layer, sequencer, filter, envelope, LFOs and FX chain are
WAVE's, and the wavetable oscillator is replaced by a grain engine. It lives
in this repository rather than in the CHOMPI checkout and is built with the
others (`chompi-sim-gui-grain`, `chompi-sim-grain`).

Sounds live in SDRAM as 48 kHz 16-bit stereo, up to ten seconds each: the
first fourteen `.wav` files on the card in name order (48 kHz 16-bit PCM, mono
or stereo; anything else is skipped, as are TAPE's `_double` copies) plus one
recorded from the inputs. `scripts/make-grain-card.py cards/grain` makes a
card from the factory TAPE samples in the checkout (bank A of the cubbi
instrument; `--instrument jammi`, `--bank b` pick others) and writes the
marker file the launcher reads the firmware name from; `--synthetic` makes
four synthetic sounds instead. A TAPE factory card works directly too.

Each held key plays a cloud of up to twelve grains from the selected sound,
at the key's pitch relative to the middle key, which plays the sound at its
own speed. The knobs:

| Knob | Page 1 | Click | Click again |
|---|---|---|---|
| PITCH | pitch, fine | scan: frozen at the left, natural speed in the middle, four times at the right | |
| A | position in the sound | spray, random spread around the position | |
| B | grain size, 5 to 500 ms | density, half a grain to eight grains at a time | |
| C | texture: fat windows to pointed ones, then a growing share of reversed grains | space, delay to the left, reverb to the right | filter cutoff |
| transport | tempo, tap to set | | |
| volume | volume | pan | |

The lower row of key LEDs shows where each sounding voice is reading in the
sound. With the mode switch up, holding CHOMPI records the microphone (or the
aux input, with a cable in the jack) into the fifteenth sound and selects it,
so you can play what you just sampled; in the simulator that is the INPUT bar.
The menu (mode switch down, CHOMPI) is WAVE's: white keys are presets, the
black keys octave, gate, the LFO switches, erase, copy and save, A and B set
attack and release, and the PITCH knob's second page picks the sound.

Over MIDI, a program change or CC 32 picks a sound by number (0 is the first
on the card, 14 the recording; empty slots are ignored), and the firmware
sends CC 32 with the selected sound whenever it changes, so a controller can
follow the menu's knob or a recording. The simulator's SOUND menu in the bar
is built on that: it lists the card's `.wav` files the way the firmware loads
them, sends a choice as CC 32 to the firmware's MIDI input and shows what the
firmware reports back. Both go on the MIDI out channel of the options file,
so keep the in and out channels alike if you change them.

`examples/grain.txt` plays the first sound, changes the cloud, samples
`examples/tone.wav` and plays it back; `examples/grain-sounds.txt` picks
sounds over MIDI. For the hardware, `firmware/grain/code/src`
builds like WAVE (`make` with the GNU Arm Embedded 10.3 toolchain, with
`LIBS_DIR` pointing at a CHOMPI `code/libs` folder); it has not been run on a
device yet, so treat it as a desktop-tested starting point and watch the CPU
load with dense clouds.

## Adding a firmware

Every firmware whose sources are in the checkout is built: a patched copy of
each goes to `build/firmware-staged-<name>` with the patches from
`firmware/patches/<name>/` applied, and the shim and simulator core are built
once and linked into one executable per firmware. `-DCHOMPI_FIRMWARES=wave;tape`
restricts the set; `-DCHOMPI_FIRMWARE=/abs/path/to/<firmware>/code` builds a
community firmware such as POLY as `custom` (patches in `firmware/patches/custom/`).
The panel's cues come from a table in `frontends/sdl/firmware_info.cpp`; a new
firmware gets a plain panel until it has an entry there.

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
  which on Linux usually sits at that very address and on macOS cannot. Its
  sample manager and slice engine do arithmetic on `void*`, a GCC extension
  that clang rejects; `firmware/patches/tempo/void_pointer_arithmetic.patch`
  spells the same byte arithmetic out with `char*` casts.

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
