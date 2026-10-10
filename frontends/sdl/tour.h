/** @file tour.h
 *  @brief The guided tour: one note at a time, each with an arrow to the
 *  control it explains, written after the official guides of each firmware.
 *  The panel draws a step (panel.cpp), the front-end steps through them and
 *  remembers which firmwares' tours were seen (main.cpp). */
#pragma once
#include <string>

namespace gui
{

/** What a tour step points at. */
struct TourTarget
{
    enum Kind
    {
        None,      /**< a note in the middle of the instrument */
        Toggle,    /**< the mode switch */
        FuncKey,   /**< a = chompi_sim::Button (KEY_CHOMPI, KEY_PLAY, KEY_LOOP) */
        Knob,      /**< a = chompi_sim::Encoder */
        WhiteKeys, /**< the white keys a..b (0..14, left to right) */
        BlackKeys, /**< the black keys a..b (0..9, left to right) */
        Keyboard,  /**< every piano key */
        PanelLeds, /**< the row of LEDs above the knobs */
        Tabs,      /**< the firmware tabs in the bar */
        Input,     /**< the INPUT section of the bar */
        Sound,     /**< the SOUND menu in the bar */
        Help,      /**< the ? button in the bar */
    };
    Kind kind = None;
    int  a    = 0;
    int  b    = 0;
};

struct TourStep
{
    TourTarget  target;
    const char* title;
    const char* body; /**< wrapped to the note's width when drawn */
    const char* keys; /**< the simulator's keys for it, or nullptr */
};

struct Tour
{
    const TourStep* steps = nullptr;
    int             count = 0;
};

/** The tour for a firmware id ("wave"); a tour of the hardware alone for any other. */
Tour TourFor(const std::string& firmware);

} // namespace gui
