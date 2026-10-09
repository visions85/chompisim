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

/** Size of the logical canvas at --scale 1. */
constexpr int kPanelW = 1120;
constexpr int kPanelH = 420;

/** Computer keyboard key printed on each piano key, indexed by semitone. */
extern const char* const kPianoKeyNames[25];

enum class HitKind
{
    None,
    PianoKey, /**< index = semitone 0..24 */
    FuncKey,  /**< index = chompi_sim::Button (KEY_PLAY / KEY_LOOP / KEY_CHOMPI) */
    Knob,     /**< index = chompi_sim::Encoder */
    Toggle,   /**< the mode switch */
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
