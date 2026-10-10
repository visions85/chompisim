/** @file firmware_info.cpp
 *  @brief Control functions per firmware, read off the firmware sources
 *  (NormalPage.h: knob pages and keys; MenuPage.h: the shift layer). */
#include "firmware_info.h"

namespace gui
{

const FirmwareInfo kFirmwares[4] = {
    {"wave", "WAVE", "1.0", "wavetable synth + sequencer", {72, 128, 232, 255},
     {{"ATTACK", "PITCH LFO", nullptr},   // A
      {"RELEASE", "FILTER LFO", nullptr}, // B
      {"DELAY FB", "FILTER", nullptr},    // C
      {"PITCH", "WAVETABLE", nullptr},    // PITCH
      {"TEMPO", nullptr, nullptr},        // transport: tap or turn
      {"VOLUME", "PAN", nullptr}},        // volume
     "MUTE / REST", "SEQ PLAY", "SEQ LOOP",
     {"OCT -", "OCT +", "GATE 10%", "GATE 50%", "GATE 100%", "PITCH LFO", "FILT LFO", "ERASE", "COPY", "SAVE"},
     "PRESET", "DEFAULT"},
    {"tape", "TAPE", "2.0", "sampler + tape looper", {236, 122, 52, 255},
     {{"START", "ATTACK", nullptr},
      {"END", "DECAY", nullptr},
      {"VERB+DELAY", "SATURATE", "FILTER"},
      {"PITCH", "LEVEL", nullptr},
      {"TAPE SPEED", nullptr, nullptr},
      {"VOLUME", nullptr, nullptr}},
     "RECORD", "LOOPER", "LOOPER REC",
     {"JAMMI BANK", "CUBBI BANK", "MIC IN", "AUX IN", "RESAMPLE", "FX PRE", "FX POST", "ERASE", "COPY", "SAVE"},
     "SLOT", "CHOMPI"},
    {"tempo", "TEMPO", "1.0", "pattern generator, two engines", {76, 188, 118, 255},
     {{"START", "ATTACK", nullptr},
      {"END", "RELEASE", nullptr},
      {"GRAIN", "GRAIN MIX", nullptr},
      {"PITCH", "VOLUME", "FILTER"},
      {"CLOCK DIV", nullptr, nullptr},
      {"VOLUME", "INPUT GAIN", nullptr}},
     "RECORD", "PATTERN", "LATCH",
     {"JAMMI BANK", "CUBBI BANK", "MIC IN", "AUX IN", "RESAMPLE", "A STATE", "B STATE", "ERASE", "COPY", "SAVE"},
     "SLOT", "CHOMPI"},
    {"grain", "GRAIN", "0.1", "granular sampler + sequencer", {214, 88, 196, 255},
     {{"POSITION", "SPRAY", nullptr},
      {"SIZE", "DENSITY", nullptr},
      {"TEXTURE", "SPACE", "FILTER"},
      {"PITCH", "SCAN", nullptr},
      {"TEMPO", nullptr, nullptr},
      {"VOLUME", "PAN", nullptr}},
     "RECORD", "SEQ PLAY", "SEQ LOOP",
     {"OCT -", "OCT +", "GATE 10%", "GATE 50%", "GATE 100%", "PITCH LFO", "FILT LFO", "ERASE", "COPY", "SAVE"},
     "PRESET", "DEFAULT"},
};

const FirmwareInfo& FirmwareByName(const std::string& id)
{
    for(const FirmwareInfo& f : kFirmwares)
        if(id == f.id)
            return f;
    static const FirmwareInfo plain = {"", "", "", "", {120, 124, 140, 255},
                                       {{nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr},
                                        {nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr}},
                                       nullptr, nullptr, nullptr,
                                       {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
                                       nullptr, nullptr};
    return plain;
}

} // namespace gui
