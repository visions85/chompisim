/** @file panel.h
 *  @brief The CHOMPI front panel: layout geometry, drawing and hit testing.
 *
 *  Everything is laid out on a logical canvas of kPanelW x kPanelH pixels;
 *  the Panel multiplies by the window scale when drawing, and HitTest() takes
 *  logical coordinates. LED colours and button states are read straight from
 *  chompi_sim::Sim; what the simulator does not know about (knob rotation,
 *  encoder push state, mouse hover, status text) comes in through UiState. */
#pragma once
#include <SDL.h>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "chompi_sim/sim.h"

namespace gui
{

/** Size of the logical canvas at --scale 1 (the firmware bar, the instrument, the text rows). */
constexpr int kPanelW = 1120;
constexpr int kPanelH = 446;

/** The computer keyboard plays a span of this many semitones, C to the F an
 *  octave and a fourth up: the home row the white keys, the row above it the
 *  black keys. UiState::piano_octave moves the span up to the top caps. */
constexpr int kPianoSpan = 18;

enum class HitKind
{
    None,
    PianoKey, /**< index = semitone 0..24 */
    FuncKey,  /**< index = chompi_sim::Button (KEY_PLAY / KEY_LOOP / KEY_CHOMPI) */
    Knob,     /**< index = chompi_sim::Encoder */
    Toggle,   /**< the mode switch */
    FirmwareTab, /**< index into gui::kFirmwares, in the bar above the instrument */
    InputButton, /**< in the bar: 0 load a sound, 1 play/stop, 2 microphone, 3 aux jack */
    SoundButton, /**< in the bar: opens and closes the sound menu */
    SoundRow,    /**< a row of the open sound menu: index into UiState::sounds */
    HelpButton,  /**< the ? in the bar: starts the guided tour */
    TourNext,    /**< while the tour is up: its NEXT button, or anywhere else in the window */
    TourBack,    /**< the tour's BACK button */
    TourClose,   /**< the tour's SKIP / DONE button */
};

struct Hit
{
    HitKind kind  = HitKind::None;
    int     index = -1;
    bool    operator==(const Hit& o) const { return kind == o.kind && index == o.index; }
    bool    operator!=(const Hit& o) const { return !(*this == o); }
};

/** A sound the firmware can select: its number in the firmware and a name to show. */
struct SoundEntry
{
    int         index;
    std::string name;
};

/** Front-end state the panel needs to draw one frame. */
struct UiState
{
    std::array<float, chompi_sim::kNumEncoders> knob_angle{};   /**< degrees, clockwise positive */
    std::array<bool, chompi_sim::kNumEncoders>  knob_pressed{}; /**< encoder push switches */
    int                                          arrow_knob = -1; /**< small knob the Left/Right arrow keys turn */
    std::array<std::string, kPianoSpan>          piano_keys{};    /**< name of the computer key on each semitone of the span ("" = none) */
    int                                          piano_octave = 0; /**< the span starts at this octave of the keybed: 0 or 1 */
    bool                                         keymap = true;   /**< draw the key map overlay */
    std::string                                  firmware;        /**< id of the firmware running ("wave"); see firmware_info.h */
    std::vector<std::string>                     firmwares_built; /**< ids the bar can switch to */
    std::string                                  card_name;       /**< card folder, shown in the bar */
    chompi_sim::InputState                       input;           /**< the sound fed to the inputs */
    bool                                         mic_open = false; /**< the computer's microphone feeds the inputs */
    float                                        input_level = 0.f; /**< meter, 0..1 */
    std::vector<SoundEntry>                      sounds;          /**< the firmware's sounds (firmwares with a sound list) */
    int                                          sound = -1;      /**< the selected one, as the firmware reports it; -1 = not yet */
    bool                                         sound_menu = false; /**< the sound menu is dropped down */
    int                                          tour_step = -1;  /**< step of the guided tour being shown, -1 = none (see tour.h) */
    bool                                         boot_window = false; /**< the simulated bootloader is waiting for a key to pick the firmware */
    chompi_sim::Rgb                              boot_led{};      /**< every LED shows this while the boot window is up */
    std::array<std::string, 4>                   boot_slots{};    /**< firmware names on the first four white caps ("" = none) */
    std::array<chompi_sim::Rgb, 4>               boot_slot_leds{}; /**< the colour each of those caps lights in: its firmware's */
    int                                          boot_choice = -1; /**< the slot picked so far, or -1 */
    std::string                                  boot_default;    /**< the firmware that boots with no key held */
    float                                        boot_left = 0.f; /**< seconds left in the window */
    Hit                                          hover;         /**< control under the mouse */
    std::string                                  status;        /**< status line */
    std::vector<std::string>                     log;           /**< last firmware log lines, oldest first */
};

class Canvas;

class Panel
{
  public:
    /** `scale` is device pixels per logical pixel. */
    Panel(SDL_Renderer* renderer, float scale);
    ~Panel();
    Panel(const Panel&)            = delete;
    Panel& operator=(const Panel&) = delete;

    /** Draws the whole window (does not present). */
    void Draw(const UiState& st);
    /** Which control is at logical position (x, y); the state says what the bar holds. */
    Hit HitTest(const UiState& st, float x, float y) const;

  private:
    std::unique_ptr<Canvas> cv_;
};

} // namespace gui
