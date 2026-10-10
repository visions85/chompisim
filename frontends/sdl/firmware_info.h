/** @file firmware_info.h
 *  @brief What the controls do in each firmware: the cues the panel draws
 *  (knob functions per click page, the function keys, the menu layer). */
#pragma once
#include <SDL.h>
#include <string>

namespace gui
{

struct FirmwareInfo
{
    const char* id;              /**< "wave", as on the command line */
    const char* name;            /**< "WAVE" */
    const char* version;
    const char* tagline;
    SDL_Color   accent;
    const char* knob[6][3];      /**< by chompi_sim::Encoder (SW1..SW6): the function on each click page */
    const char* chompi;          /**< the CHOMPI key with the mode switch up */
    const char* play;
    const char* loop;
    const char* menu_black[10];  /**< the black keys, left to right, in the menu (mode switch down + CHOMPI) */
    const char* menu_white;      /**< the white keys in the menu, numbered 1..14 */
    const char* menu_white15;    /**< the fifteenth white key in the menu */
};

/** The firmwares the panel knows; FirmwareByName() falls back to a plain entry. */
const FirmwareInfo& FirmwareByName(const std::string& id);
extern const FirmwareInfo kFirmwares[4];

} // namespace gui
