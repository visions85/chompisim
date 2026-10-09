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

/** Computer keyboard key printed on each piano key, indexed by semitone. */
extern const char* const kPianoKeyNames[25];

enum class HitKind
{
    None,
    PianoKey, /**< index = semitone 0..24 */
    FuncKey,  /**< index = chompi_sim::Button (KEY_PLAY / KEY_LOOP / KEY_CHOMPI) */
    Knob,     /**< index = chompi_sim::Encoder */
    Toggle,   /**< the mode switch */
    FirmwareTab, /**< index into gui::kFirmwares, in the bar above the instrument */
    InputButton, /**< in the bar: 0 load a sound, 1 play/stop, 2 microphone, 3 aux jack */
};

struct Hit
{
    HitKind kind  = HitKind::None;
    int     index = -1;
    bool    operator==(const Hit& o) const { return kind == o.kind && index == o.index; }
    bool    operator!=(const Hit& o) const { return !(*this == o); }
};

/** Front-end state the panel needs to draw one frame. */
struct UiState
{
    std::array<float, chompi_sim::kNumEncoders> knob_angle{};   /**< degrees, clockwise positive */
    std::array<bool, chompi_sim::kNumEncoders>  knob_pressed{}; /**< encoder push switches */
    int                                          arrow_knob = -1; /**< small knob the Left/Right arrow keys turn */
    bool                                         keymap = true;   /**< draw the key map overlay */
    std::string                                  firmware;        /**< id of the firmware running ("wave"); see firmware_info.h */
    std::vector<std::string>                     firmwares_built; /**< ids the bar can switch to */
    std::string                                  card_name;       /**< card folder, shown in the bar */
    chompi_sim::InputState                       input;           /**< the sound fed to the inputs */
    bool                                         mic_open = false; /**< the computer's microphone feeds the inputs */
    float                                        input_level = 0.f; /**< meter, 0..1 */
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
    /** Which control is at logical position (x, y). */
    Hit HitTest(float x, float y) const;

  private:
    std::unique_ptr<Canvas> cv_;
};

} // namespace gui
