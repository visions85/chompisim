/** @file tour.cpp
 *  @brief The tour steps, per firmware. The wording follows the official
 *  guides: the TAPE 2.0 Guidebook and Mini Guide, the TEMPO Guidebook and
 *  the WAVE quick start (chompiclub.com/manuals); the simulator's own keys
 *  and bar are added where they stand in for the hardware. */
#include "tour.h"
#include "chompi_sim/sim.h"

namespace gui
{
using namespace chompi_sim;

namespace
{

constexpr TourTarget kNone{TourTarget::None, 0, 0};
constexpr TourTarget kToggle{TourTarget::Toggle, 0, 0};
constexpr TourTarget kChompi{TourTarget::FuncKey, KEY_CHOMPI, 0};
constexpr TourTarget kPlay{TourTarget::FuncKey, KEY_PLAY, 0};
constexpr TourTarget kLoop{TourTarget::FuncKey, KEY_LOOP, 0};
constexpr TourTarget kPitch{TourTarget::Knob, ENC_SW4, 0};
constexpr TourTarget kKnobA{TourTarget::Knob, ENC_SW1, 0};
constexpr TourTarget kKnobB{TourTarget::Knob, ENC_SW2, 0};
constexpr TourTarget kKnobC{TourTarget::Knob, ENC_SW3, 0};
constexpr TourTarget kTransport{TourTarget::Knob, ENC_SW5, 0};
constexpr TourTarget kVolume{TourTarget::Knob, ENC_SW6, 0};
constexpr TourTarget kKeyboard{TourTarget::Keyboard, 0, 0};
constexpr TourTarget kWhiteSlots{TourTarget::WhiteKeys, 0, 13};
constexpr TourTarget kKey15{TourTarget::WhiteKeys, 14, 14};
constexpr TourTarget kBlack12{TourTarget::BlackKeys, 0, 1};
constexpr TourTarget kBlack345{TourTarget::BlackKeys, 2, 4};
constexpr TourTarget kBlack67{TourTarget::BlackKeys, 5, 6};
constexpr TourTarget kBlack8910{TourTarget::BlackKeys, 7, 9};
constexpr TourTarget kLeds{TourTarget::PanelLeds, 0, 0};
constexpr TourTarget kTabs{TourTarget::Tabs, 0, 0};
constexpr TourTarget kInput{TourTarget::Input, 0, 0};
constexpr TourTarget kSound{TourTarget::Sound, 0, 0};
constexpr TourTarget kHelp{TourTarget::Help, 0, 0};

/** Lines every tour shares. */
constexpr const char* kNavigate = "Click anywhere or press the right arrow to go on, the left arrow to go back, "
                                  "ESC to leave. The ? in the bar brings the tour back any time.";
constexpr const char* kKeyboardKeys = "a s d f g h j k l ; ' play the white keys and w e t y u o p the black ones; "
                                      "x shifts them up an octave to reach the top caps, z back down.";
constexpr const char* kToggleKeys = "Tab flips the switch (it latches); the caps show their menu names while it is down.";
constexpr const char* kChompiKeys = "Left Shift holds the CHOMPI key.";
constexpr const char* kTransportKeys = "[ and ] turn it, F5 presses it.";
constexpr const char* kVolumeKeys = "- and = turn it, F6 presses it.";
constexpr const char* kKnobKeys = "Drag up or down on a knob or scroll over it to turn it; click it to press it, "
                                  "right-click to hold it down. F1 to F4 press the small knobs.";
constexpr const char* kInputBody =
    "A laptop has no sample inputs, so this section feeds the firmware's microphone and aux input: LOAD a WAV "
    "(or drop one on the window), PLAY and STOP it, MIC opens the computer's microphone, JACK plugs the aux "
    "cable in or out. The meter shows the level reaching the firmware.";
constexpr const char* kInputKeys = "F10 loads a sound, F7 and F8 play and stop it, F9 opens and closes the microphone.";
constexpr const char* kTabsBody = "These reboot the simulator into another firmware, each with its own card folder "
                                  "(--cards names the folder that holds them). The labels on the panel change with "
                                  "the firmware: each knob's pages, the three keys and the menu on the caps.";
constexpr const char* kLedsBody =
    "The LED above each knob shows which page it is on and where the control sits, in the colours the guides "
    "describe; the two small LEDs by the transport knob belong to it. Every LED here is read from the firmware.";
constexpr const char* kEndBody = "Press ? in the bar to see this tour again. / shows the key map, with every "
                                 "computer key drawn on the control it works, and the lines at the bottom list them.";

// ---------------------------------------------------------------------------
// TAPE 2.0 (the TAPE Guidebook, levels 1 to 7)
// ---------------------------------------------------------------------------
const TourStep kTape[] = {
    {kNone, "TAPE: the sampler",
     "TAPE is the firmware every CHOMPI ships with: a 7-voice sampler and a varispeed tape looper. This tour "
     "walks the panel the way the official Guidebook does, one control at a time.",
     kNavigate},
    {kToggle, "MODE switch",
     "UP is CHOMPI mode: the input is monitored and the CHOMPI key records. DOWN is JAMMI mode: the monitor "
     "is off, the knobs shape the sound and the CHOMPI key becomes a SHIFT key.",
     kToggleKeys},
    {kChompi, "CHOMPI key",
     "Switch UP: hold it to record. Recording stops when you let go and the sound is on the keyboard right "
     "away. Switch DOWN: hold it as SHIFT for the second function of every knob and the menu on the black keys.",
     "Left Shift holds it. Give it something to record with the INPUT buttons in the bar first."},
    {kKeyboard, "The keyboard",
     "Plays the selected sample chromatically across two octaves: higher keys play it faster and higher, "
     "lower keys slower and lower. Up to seven keys sound at once.",
     kKeyboardKeys},
    {kKey15, "The CHOMPI buffer",
     "The fifteenth white key holds whatever you recorded last, until you power off or record again. Save it "
     "to a preset slot to keep it (SAVE, in the shift menu).",
     nullptr},
    {kPitch, "SPEED and direction",
     "Page 1: playback speed and direction of the selected sample. Right is faster and higher, left slower and "
     "lower, and on into reverse. SHIFT + turn steps in fifths and octaves; SHIFT + press resets to 1x forward "
     "(green LED). Press for page 2: the sample's own volume (blue 0%, pink 100%, red 200%); SHIFT + turn "
     "there pans it.",
     kKnobKeys},
    {kKnobA, "SAMPLE START",
     "Page 1 (yellow LED): where the sample starts playing; turn clockwise to nudge the start point in. "
     "Press for page 2 (blue): ATTACK, how long the sample takes to reach full volume. SHIFT + press on "
     "page 1 switches sample looping on and off.",
     nullptr},
    {kKnobB, "SAMPLE END",
     "Page 1 (red LED): where the sample ends or loops; turn counter-clockwise to pull the end in. Page 2 "
     "(purple): DECAY. SHIFT + turn moves start and end together; SHIFT + press switches sustain (key hold) "
     "on and off.",
     nullptr},
    {kKnobC, "MULTI-FX, the magic wand",
     "Page 1 (blue): delay + reverb, more as you turn right; SHIFT + turn sets the delay time. Page 2 "
     "(yellow): tape saturation and compression; SHIFT + turn adds wow and flutter. Page 3 (pink): the "
     "filter, low-pass left of noon, high-pass right of it; SHIFT + turn is resonance. SHIFT + press resets "
     "all three.",
     nullptr},
    {kLoop, "LOOP key",
     "First press: the loop starts recording (red LED). Second press: sets the loop length and starts "
     "overdubbing on top of it (yellow). PLAY or LOOP again stops overdubbing while the loop keeps playing "
     "(white). Hold PLAY + LOOP for two seconds to erase the loop.",
     "Return is the LOOP key."},
    {kPlay, "PLAY / PAUSE key",
     "Plays and pauses the loop. Hold PLAY and tap LOOP to arm recording, so the loop starts with your first "
     "note. While overdubbing, SHIFT + PLAY fades the older layers by 10% a pass (SHIFT + LOOP brings it "
     "back up).",
     "Space is the PLAY key."},
    {kTransport, "TRANSPORT knob",
     "While the loop plays: its speed and direction. Right speeds up (blue), left slows down (red) and on "
     "into reverse; press to come back to the recorded speed. While the loop is paused it scrubs the tape by "
     "hand. SHIFT + turn steps in fifths and octaves.",
     kTransportKeys},
    {kVolume, "VOLUME knob",
     "Page 1: output volume; the LED is a VU meter, white to green to yellow, red when it clips. Press for "
     "page 2: the input gain of the microphone or aux input. SHIFT + turn: the master compressor. SHIFT + "
     "press cycles the three input monitor routings. Hold it two seconds for a battery check.",
     kVolumeKeys},
    {kBlack12, "Menu: JAMMI / CUBBI banks",
     "Switch DOWN and CHOMPI held, the black keys are a menu. The first two pick the sample engine and bank: "
     "JAMMI plays one sample chromatically, CUBBI gives each white key its own one-shot. Each press cycles "
     "the banks A to E (purple, gold, teal, orange, lime); then a white key 1 to 14 picks the slot.",
     "Tab, hold Left Shift, then w or e, then a white key."},
    {kBlack345, "Menu: input select",
     "Which source the sampler records: the microphone, the AUX input (chosen by itself when a cable is in "
     "the jack) or RE-SAMPLE, which feeds the master output back into the sampler.",
     "The JACK button in the bar plugs the simulated aux cable in and out."},
    {kBlack67, "Menu: FX position",
     "PRE puts the effects before the looper, so they are recorded into your loops; POST applies them to "
     "everything after the looper, so you can layer effects on effects.",
     nullptr},
    {kBlack8910, "Menu: ERASE, COPY, SAVE",
     "Hold CHOMPI and press SAVE (blue): the buffer is the source. Let go of CHOMPI when the keys blink, pick "
     "a bank with the JAMMI or CUBBI key, press a white key 1 to 14 for the slot, then press CHOMPI to "
     "confirm. COPY (green) copies an existing slot the same way; ERASE (red) clears one.",
     "Tab, hold Left Shift, u (SAVE, with x pressed first for the top black keys), release, a white key, Left Shift."},
    {kLeds, "The LEDs", kLedsBody, nullptr},
    {kInput, "INPUT, in the simulator", kInputBody, kInputKeys},
    {kTabs, "Other firmwares", kTabsBody, nullptr},
    {kHelp, "That is the tour", kEndBody, nullptr},
};

// ---------------------------------------------------------------------------
// WAVE 1.0 (the WAVE quick start guide)
// ---------------------------------------------------------------------------
const TourStep kWave[] = {
    {kNone, "WAVE: the wavetable synth",
     "WAVE is an 8-voice wavetable synthesizer: each voice is a wavetable oscillator through a resonant "
     "filter and an amp envelope, into delay, reverb, compression and saturation, with two LFOs, a 32-step "
     "sequencer and 15 preset slots. This tour follows the official quick start guide.",
     kNavigate},
    {kToggle, "MODE switch",
     "UP: play mode, and the CHOMPI key is a REST for the sequencer. DOWN: the shift menu. Hold the CHOMPI "
     "key as SHIFT and the white keys recall presets, the black keys hold the deeper controls and the knobs "
     "take their second functions.",
     kToggleKeys},
    {kChompi, "CHOMPI key",
     "Switch UP: adds a rest while the sequencer is recording, and mutes the sequence while it plays. Switch "
     "DOWN: SHIFT.",
     kChompiKeys},
    {kKeyboard, "The keyboard", "Two octaves of notes, eight voices at a time.", kKeyboardKeys},
    {kPitch, "PITCH knob",
     "Page 1: fine tune (SHIFT + turn: half-steps). Press for page 2 and turn: you scan through the 33 "
     "waves of the wavetable, and the sound morphs, thin to thick, hollow to bright. SHIFT + turn there jumps "
     "to another wavetable: the factory card holds Classic Console, Harmonic Bloom, FM Bells & Metal, "
     "Wavefolder, Vowels, Degradation and Sample Platter.",
     kKnobKeys},
    {kKnobA, "ATTACK knob",
     "Page 1: the envelope's attack (SHIFT + turn: coarse). Page 2: the depth of the pitch LFO; SHIFT + turn "
     "sets its rate, from a slow seven-second sweep up to audio rate. SHIFT + press on page 2 puts depth and "
     "rate back to their defaults.",
     nullptr},
    {kKnobB, "DECAY knob",
     "Page 1: the envelope's release (SHIFT + turn: coarse). Page 2: the depth of the filter LFO; SHIFT + "
     "turn sets its rate.",
     nullptr},
    {kKnobC, "EFFECTS knob",
     "Page 1: left of noon delay, right of noon reverb; SHIFT + turn sets the delay time or the reverb size. "
     "Page 2: the filter cutoff; SHIFT + turn is resonance.",
     nullptr},
    {kTransport, "TRANSPORT knob: tempo",
     "Turn for the sequencer's tempo, or press it two or more times in a row to tap one in. SHIFT + turn: "
     "clock divide and multiply.",
     kTransportKeys},
    {kVolume, "VOLUME knob",
     "Page 1: volume. Page 2: pan. SHIFT + turn on either page is the compressor.", kVolumeKeys},
    {kPlay, "PLAY key",
     "Starts and stops the sequencer's clock. MIDI start, stop and a 24 PPQN clock go out with it.",
     "Space is the PLAY key."},
    {kLoop, "LOOP key: record a sequence",
     "Arms recording: every note you press is added as a step, up to 32. Hold LOOP to delete the last step; "
     "hold PLAY + LOOP together to clear the whole sequence.",
     "Return is the LOOP key."},
    {kWhiteSlots, "Presets",
     "Hold SHIFT (switch DOWN) and the 15 white keys recall preset slots. Slot 15 is the default slot: it is "
     "always there and cannot be saved over. The last three black keys ERASE, COPY and SAVE slots.",
     "Tab, hold Left Shift, then a white key."},
    {kBlack12, "Menu: octaves",
     "SHIFT + the two leftmost black keys move the keyboard an octave down or up; MIDI notes follow.",
     "Tab, hold Left Shift, then w or e."},
    {kBlack345, "Menu: gate length",
     "How long each sequenced note is held: 10% is a rapid pluck, 50% half a step, 100% legato where every "
     "note runs into the next.",
     nullptr},
    {kBlack67, "Menu: the LFOs",
     "SHIFT + the PITCH or FILTER key switches that LFO on (yellow LED above the key) or off. Their depth "
     "and rate live on page 2 of the ATTACK and DECAY knobs.",
     nullptr},
    {kBlack8910, "Menu: ERASE, COPY, SAVE",
     "Hold SHIFT and press one of them, then the white key of the slot, then CHOMPI to confirm.",
     "The top three black keys are t y u with x pressed first (octave up)."},
    {kLeds, "The LEDs", kLedsBody, nullptr},
    {kTabs, "Other firmwares", kTabsBody, nullptr},
    {kHelp, "That is the tour", kEndBody, nullptr},
};

// ---------------------------------------------------------------------------
// TEMPO 1.0 (the TEMPO Guidebook)
// ---------------------------------------------------------------------------
const TourStep kTempo[] = {
    {kNone, "TEMPO: pattern generator",
     "TEMPO reimagines the instrument as a groovebox: two sample engines, CHROMA (one sample across the "
     "keyboard) and SLICE (one sample chopped into sixteen slices), each with its own pattern, on one "
     "master clock that also times the delays. This tour follows the official Guidebook.",
     kNavigate},
    {kToggle, "MODE switch",
     "UP is CHOMPI mode: the input is monitored and the CHOMPI key records. DOWN is JAMMI mode: the CHOMPI "
     "key is SHIFT, the knobs shape the sound and the black keys are the menu.",
     kToggleKeys},
    {kChompi, "CHOMPI key",
     "Switch UP: hold it to record a sample; it is playable the moment you let go and lives in the buffer "
     "(the fifteenth white key) until you save it. Switch DOWN: SHIFT. SHIFT + press on a knob resets that "
     "page.",
     "Left Shift holds it; feed it a sound with the INPUT buttons in the bar."},
    {kKeyboard, "The keyboard",
     "CHROMA engine: the sample across two octaves on black and white keys. SLICE engine: each white key "
     "fires one of the sixteen slices (the last key alternates slices 15 and 16) and the black keys add "
     "rests to the pattern.",
     kKeyboardKeys},
    {kPlay, "PLAY / PAUSE key",
     "Runs the pattern generator and the clock; the LEDs by the TEMPO knob blink the beat. SHIFT + press "
     "cycles five arp styles per engine: note order, up, down, ping-pong, random. A long press is freestyle: "
     "the engine leaves the arpeggiator so you can play the keys freely.",
     "Space is the PLAY key."},
    {kLoop, "LOOP key: latch",
     "Press once and every key you play joins the pattern, in the order entered (latched keys light up: red "
     "for CHROMA, yellow for SLICE). Press a latched key again to drop it; press LOOP again to clear. "
     "SHIFT + press cycles five rest patterns. A long press while stopped is key sustain (orange LED).",
     "Return is the LOOP key."},
    {kTransport, "TEMPO knob",
     "The master clock, 60 to 240 BPM; press it two or more times to tap a tempo. Press and turn: clock "
     "division or multiplication for the selected engine. SHIFT + turn: randomization of the sequence. "
     "SHIFT + long press: follow an external MIDI clock.",
     kTransportKeys},
    {kPitch, "SAMPLE STAGING knob",
     "Page 1: speed and direction (SHIFT + turn steps in fifths and octaves). Page 2: the sample's volume "
     "(SHIFT + turn: pan). Page 3: the multi-mode filter, low-pass left, high-pass right (SHIFT + turn: "
     "sample-rate reduction).",
     kKnobKeys},
    {kKnobA, "SAMPLE START knob",
     "Page 1: the start point (SHIFT + turn moves the whole window). Page 2: attack (SHIFT + turn switches "
     "sample looping on and off). Push either envelope knob to an extreme with looping on and a tiny loop "
     "becomes an oscillator.",
     nullptr},
    {kKnobB, "SAMPLE END knob",
     "Page 1: the end point (SHIFT + turn: a coarse window, halved each notch). Page 2: release (SHIFT + "
     "turn switches sustain on and off).",
     nullptr},
    {kKnobC, "DUAL DELAY FX knob",
     "Left of noon: a clock-synced delay and its interval. Right of noon: delay with a growing share of "
     "diffusion reverb. SHIFT + turn: randomization. A short press freezes the buffer (blinking LED). A long "
     "press opens the hidden page, the dry/wet mix per engine; SHIFT + turn there is feedback.",
     nullptr},
    {kVolume, "VOLUME knob",
     "Page 1: master volume, the LED a VU meter. Page 2: input gain. SHIFT + turn: master compression, with "
     "dirt past 50%. SHIFT + press: the input monitor routing, positions 1 to 3.",
     kVolumeKeys},
    {kBlack12, "Menu: CHROMA / SLICE",
     "Each engine has one bank of 14 slots. Hold SHIFT, press the engine's key, then a white key 1 to 14; "
     "let go of SHIFT and play.",
     "Tab, hold Left Shift, then w or e, then a white key."},
    {kBlack345, "Menu: input select",
     "Which source the sampler records: the microphone, the AUX input (chosen by itself when a cable is in "
     "the jack) or RE-SAMPLE, the master output fed back in.",
     "The JACK button in the bar plugs the simulated aux cable in and out."},
    {kBlack67, "Menu: snapshots A and B",
     "Two complete states of the device. SHIFT + press switches between them instantly; SHIFT + hold copies "
     "the one you hold onto the other (the target flashes green). A is live at boot, B empty; both reset "
     "at power-off.",
     nullptr},
    {kBlack8910, "Menu: ERASE, COPY, SAVE",
     "Hold SHIFT and press SAVE (blue), let go when the keys blink, pick CHROMA or SLICE with the engine "
     "keys, press a white key for the slot and CHOMPI to confirm. COPY (green) asks for a source slot "
     "first; ERASE (red) clears a slot.",
     "The top three black keys are t y u with x pressed first (octave up)."},
    {kLeds, "The LEDs", kLedsBody, nullptr},
    {kInput, "INPUT, in the simulator", kInputBody, kInputKeys},
    {kTabs, "Other firmwares", kTabsBody, nullptr},
    {kHelp, "That is the tour", kEndBody, nullptr},
};

// ---------------------------------------------------------------------------
// GRAIN 0.1 (this repository's granular sampler, see the README)
// ---------------------------------------------------------------------------
const TourStep kGrain[] = {
    {kNone, "GRAIN: granular sampler",
     "GRAIN is grown from WAVE: each held key plays a cloud of up to twelve grains from the selected sound at "
     "the key's pitch, and the sequencer, presets, filter, envelope, LFOs and effects are WAVE's. It lives in "
     "this repository and has not run on a real instrument yet.",
     kNavigate},
    {kToggle, "MODE switch",
     "UP: play mode, and the CHOMPI key records. DOWN: the shift menu, with the CHOMPI key held as SHIFT: "
     "white keys recall presets, black keys hold the deeper controls.",
     kToggleKeys},
    {kChompi, "CHOMPI key: record",
     "Switch UP: hold it to record the inputs into the fifteenth sound and select it, so you can play what "
     "you just sampled. Switch DOWN: SHIFT, and a rest while the sequencer records.",
     "Left Shift holds it; feed it a sound with the INPUT buttons in the bar."},
    {kKeyboard, "The keyboard",
     "The middle key plays the sound at its own speed; the others pitch it. The LEDs of the lower row show "
     "where each sounding voice is reading in the sound.",
     kKeyboardKeys},
    {kSound, "SOUND menu",
     "The fourteen sounds on the card, in name order, plus the recording. Pick one here; on the instrument "
     "the second page of the PITCH knob does it in the shift menu, and MIDI program change or CC 32 "
     "selects by number.",
     nullptr},
    {kPitch, "PITCH knob",
     "Page 1: fine pitch. Page 2: SCAN, how the playhead moves through the sound: frozen at the left, "
     "natural speed in the middle, four times as fast at the right.",
     kKnobKeys},
    {kKnobA, "POSITION knob",
     "Page 1: where in the sound the grains are taken from. Page 2: SPRAY, a random spread around that "
     "position.",
     nullptr},
    {kKnobB, "SIZE knob",
     "Page 1: grain length, 5 to 500 ms. Page 2: DENSITY, from half a grain at a time to eight overlapping.",
     nullptr},
    {kKnobC, "TEXTURE knob",
     "Page 1: the grain window, fat to pointed, then past the middle a growing share of reversed grains. "
     "Page 2: SPACE, delay left of noon, reverb right of it. Page 3: the filter cutoff.",
     nullptr},
    {kTransport, "TRANSPORT knob: tempo",
     "The sequencer's tempo; press it two or more times in a row to tap one in.", kTransportKeys},
    {kVolume, "VOLUME knob", "Page 1: volume. Page 2: pan. SHIFT + turn: the compressor.", kVolumeKeys},
    {kPlay, "PLAY key", "Starts and stops the sequencer.", "Space is the PLAY key."},
    {kLoop, "LOOP key: record a sequence",
     "Arms recording: every note you press is added as a step, up to 32. Hold LOOP to delete the last step; "
     "hold PLAY + LOOP to clear the sequence.",
     "Return is the LOOP key."},
    {kWhiteSlots, "Presets",
     "Hold SHIFT (switch DOWN) and the white keys recall preset slots; slot 15 is the default. A and B set "
     "attack and release in the menu, and the last three black keys ERASE, COPY and SAVE slots.",
     "Tab, hold Left Shift, then a white key."},
    {kBlack12, "Menu: octaves, gate, LFOs",
     "The first two black keys move the keyboard an octave down or up, the next three set the gate length "
     "(10%, 50%, 100%) of sequenced notes, and the two after them switch the pitch and filter LFOs on and off.",
     "Tab, hold Left Shift, then w e t y u o p."},
    {kLeds, "The LEDs", kLedsBody, nullptr},
    {kInput, "INPUT, in the simulator", kInputBody, kInputKeys},
    {kTabs, "Other firmwares", kTabsBody, nullptr},
    {kHelp, "That is the tour", kEndBody, nullptr},
};

// ---------------------------------------------------------------------------
// Any other firmware: the hardware alone
// ---------------------------------------------------------------------------
const TourStep kPlain[] = {
    {kNone, "The panel",
     "This firmware is not one the tour knows, so here is the hardware: a two-row keyboard of 25 keys, "
     "five push-button knobs, a transport knob, three keys and a mode switch, all lit by LEDs the firmware "
     "drives.",
     kNavigate},
    {kToggle, "MODE switch", "A two-position slide switch; the firmwares use it to pick a mode.", kToggleKeys},
    {kChompi, "CHOMPI key", "The key the firmwares use for recording and as SHIFT.", kChompiKeys},
    {kKeyboard, "The keyboard", "Fifteen white keys and ten black keys, each with an RGB LED under the cap.",
     kKeyboardKeys},
    {kPitch, "The knobs",
     "Endless encoders with a push button: turn to adjust, press to change page. The LED above each one "
     "shows the page and the position.",
     kKnobKeys},
    {kTransport, "TRANSPORT knob", "The big encoder; its two small LEDs sit beside it.", kTransportKeys},
    {kVolume, "VOLUME knob", "The last small encoder.", kVolumeKeys},
    {kPlay, "PLAY key", "Space presses it.", nullptr},
    {kLoop, "LOOP key", "Return presses it.", nullptr},
    {kInput, "INPUT, in the simulator", kInputBody, kInputKeys},
    {kTabs, "Other firmwares", kTabsBody, nullptr},
    {kHelp, "That is the tour", kEndBody, nullptr},
};

template <size_t N>
Tour Make(const TourStep (&steps)[N])
{
    return Tour{steps, int(N)};
}

} // namespace

Tour TourFor(const std::string& firmware)
{
    if(firmware == "tape")
        return Make(kTape);
    if(firmware == "wave")
        return Make(kWave);
    if(firmware == "tempo")
        return Make(kTempo);
    if(firmware == "grain")
        return Make(kGrain);
    return Make(kPlain);
}

} // namespace gui
