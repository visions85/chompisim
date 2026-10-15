/** @file lesson.cpp
 *  @brief The lessons: making a drone on DRONE, and first steps on any firmware. */
#include "lesson.h"
#include "chompi_sim/sim.h"

using namespace chompi_sim;

namespace gui
{
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
constexpr TourTarget kVolume{TourTarget::Knob, ENC_SW6, 0};
constexpr TourTarget kKeyboard{TourTarget::Keyboard, 0, 0};
constexpr TourTarget kHelp{TourTarget::Help, 0, 0};

constexpr const char* kLessonKeys = "Page Down skips a step, Page Up goes back, ESC ends the lesson.";
constexpr const char* kKnobTurnKeys = "Drag up on the knob, or scroll over it.";

using C = LessonCheck;

// ---------------------------------------------------------------------------
// DRONE: a drone, step by step
// ---------------------------------------------------------------------------
const LessonStep kDrone[] = {
    {kNone, "A drone, step by step",
     "This lesson has you make a drone on the instrument. Each step asks for one thing; the note ticks it "
     "off when you have done it and moves on by itself. The instrument plays all the way through.",
     kLessonKeys, {C::Click}},
    {kToggle, "The mode switch down",
     "Flip the mode switch down: the shift menu. With it down the caps show their menu names and the CHOMPI "
     "key is SHIFT.",
     "Tab flips it.", {C::SwitchDown}},
    {kChompi, "Hold CHOMPI", "Hold the CHOMPI key: the menu layer lights up on the keys, and the LOOP key reads LATCH.",
     "Hold Left Shift.", {C::Hold, KEY_CHOMPI, 300, C::SwitchIsDown}},
    {kLoop, "LATCH on",
     "Still holding CHOMPI, press LOOP: latch on (the LOOP LED turns teal). A latched key keeps sounding after "
     "you let go, until you press it again.",
     "Keep Left Shift held and press Return.", {C::Press, KEY_LOOP, 0, C::Menu}},
    {kKnobA, "A slow attack",
     "Still holding CHOMPI, turn the A knob up: in the menu it sets the attack, up to 30 seconds. A few "
     "detents give a fade-in of several seconds.",
     "Keep Left Shift held; drag up on the A knob, or scroll over it.", {C::Turn, ENC_SW1, 3, C::Menu}},
    {kKnobB, "A long release", "And the B knob: the release, up to 30 seconds. Then let go of CHOMPI.",
     "Drag up on the B knob, or scroll over it.", {C::Turn, ENC_SW2, 3, C::Menu}},
    {kToggle, "Back to play mode", "Flip the switch up: play mode, with latch, the slow attack and the long release set.",
     "Tab.", {C::SwitchUp}},
    {kKeyboard, "A chord",
     "Tap three keys, one after the other, and let go: latched, each fades in over the attack and stays. A "
     "fifth and an octave above the first key sit well.",
     "a, h and k, for example (the white keys are a s d f g h j k l ; ').", {C::Keys, 3, 0, C::SwitchIsUp}},
    {kPitch, "The DRIFT page",
     "Click the PITCH knob until the LED above it turns teal: page 3, DRIFT (page 2, SCAN, lights it pink).",
     "Click the knob, or press F4.", {C::Push, ENC_SW4, 1, C::SwitchIsUp}},
    {kPitch, "Drift up",
     "Turn it up: every voice wanders on its own, in position, pitch and pan, so the chord keeps changing.",
     kKnobTurnKeys, {C::Turn, ENC_SW4, 10, C::SwitchIsUp}},
    {kKnobA, "DETUNE",
     "Click the A knob until its LED turns blue: page 3, DETUNE (page 2, SPRAY, is purple). Turn it up a "
     "little: unison, every grain detuned at random and spread wider across the field.",
     "Click the knob or press F1, then drag up or scroll.", {C::Turn, ENC_SW1, 3, C::SwitchIsUp}},
    {kKnobC, "SPACE: reverb",
     "Click the C knob once: page 2, SPACE. Turn it right of the middle: reverb (left of the middle is "
     "delay).",
     "Click the knob or press F3, then drag up or scroll.", {C::Turn, ENC_SW3, 3, C::SwitchIsUp}},
    {kKnobC, "SHIMMER",
     "Click C twice more: page 4, SHIMMER (the LED turns purple). Turn it up: the reverb comes back into "
     "itself an octave up, and the drone blooms.",
     "Click twice, or F3 F3, then drag up or scroll.", {C::Turn, ENC_SW3, 3, C::SwitchIsUp}},
    {kPlay, "The looper",
     "Flip the switch down, hold CHOMPI and press PLAY: the looper starts a loop sixteen beats long (twelve "
     "seconds at the default tempo), empty, recording what you play on top of what it already holds. Its LED "
     "turns amber.",
     "Tab, hold Left Shift, Space.", {C::Press, KEY_PLAY, 0, C::Menu}},
    {kKeyboard, "Play over the loop",
     "Flip the switch up and play a couple of notes, or tap a latched key to end it: everything you do comes "
     "round every pass, fading a little each time.",
     "Tab, then keys.", {C::Keys, 2, 0, C::SwitchIsUp}},
    {kPlay, "Take a pass back",
     "Flip the switch down, hold CHOMPI and hold PLAY for half a second: the last pass is taken back (the "
     "LED flashes white). Holding it again right after brings the pass back.",
     "Tab, hold Left Shift, hold Space.", {C::Hold, KEY_PLAY, 600, C::Menu}},
    {kChompi, "Resample",
     "Flip the switch up, hold CHOMPI, and press PLAY while holding it: the instrument records what it plays "
     "into the fifteenth sound (the CHOMPI LED pulses magenta). Let go after a few seconds, and that "
     "recording is the sound under the keys: a drone to drone on.",
     "Tab, hold Left Shift, Space, then let go of Left Shift.", {C::Press, KEY_PLAY, 0, C::ChompiUp}},
    {kHelp, "That is a drone",
     "Keep going: SAVE in the menu keeps the settings in a preset slot, the SOUND menu picks another sound to "
     "drone on, and the ? tour explains every control. ESC ends the lesson.",
     nullptr, {C::Click}},
};

// ---------------------------------------------------------------------------
// Any firmware: first steps on the instrument
// ---------------------------------------------------------------------------
const LessonStep kFirstSteps[] = {
    {kNone, "First steps",
     "This lesson has you try the instrument. Each step asks for one thing; the note ticks it off when it "
     "sees it done and moves on by itself.",
     kLessonKeys, {C::Click}},
    {kKeyboard, "Play a key", "Press a key on the keybed.",
     "a s d f g h j k l ; ' are the white keys, w e t y u o p the black ones; x shifts them up an octave.",
     {C::Keys, 1}},
    {kKeyboard, "Three keys", "Press three different keys, together or one after the other.", nullptr, {C::Keys, 3}},
    {kVolume, "The volume", "Turn the VOLUME knob a few detents, either way.",
     "- and = turn it; or drag up and down on it, or scroll over it.", {C::TurnAny, ENC_SW6, 3}},
    {kKnobA, "A knob's pages",
     "Click the A knob: it goes to its next page and the LED above it changes colour. The cue under each knob "
     "lists what its clicks reach. Click it again to come back.",
     "Click it, or press F1.", {C::Push, ENC_SW1, 1}},
    {kToggle, "The mode switch", "Flip the mode switch down and hold CHOMPI: the caps show the menu layer.",
     "Tab, then hold Left Shift.", {C::Hold, KEY_CHOMPI, 300, C::SwitchIsDown}},
    {kToggle, "And back", "Let go of CHOMPI and flip the switch back up.", "Tab.", {C::SwitchUp}},
    {kHelp, "Where to go from here",
     "The ? in the bar starts the tour of this firmware's controls, / shows the key map, and the tabs switch "
     "firmwares. ESC ends the lesson.",
     nullptr, {C::Click}},
};

template <size_t N>
Lesson Make(const char* name, const LessonStep (&steps)[N])
{
    return Lesson{name, steps, int(N)};
}

} // namespace

Lesson LessonFor(const std::string& firmware)
{
    if(firmware == "drone")
        return Make("Make a drone", kDrone);
    return Make("First steps", kFirstSteps);
}

} // namespace gui
