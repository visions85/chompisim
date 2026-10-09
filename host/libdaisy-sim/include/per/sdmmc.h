#pragma once
#ifndef DSY_SDMMC_H
#define DSY_SDMMC_H
#include "daisy_core.h"
namespace daisy
{
/** SD/MMC peripheral. The simulated card is a folder on the host (Config::card_dir). */
class SdmmcHandler
{
  public:
    enum class Result { OK, ERROR };
    enum class BusWidth { BITS_1, BITS_4 };
    enum class Speed { SLOW, MEDIUM_SLOW, STANDARD, FAST, VERY_FAST };
    struct Config
    {
        Speed    speed;
        BusWidth width;
        bool     clock_powersave;
        void     Defaults()
        {
            speed           = Speed::FAST;
            width           = BusWidth::BITS_4;
            clock_powersave = false;
        }
    };
    SdmmcHandler() {}
    ~SdmmcHandler() {}
    Result Init(const Config& cfg);
};
} // namespace daisy
#endif
