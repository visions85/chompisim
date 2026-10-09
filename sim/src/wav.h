/** @file wav.h
 *  @brief Minimal WAV reader for the audio-input feed: RIFF/WAVE with PCM
 *  (8/16/24/32-bit) or IEEE float (32/64-bit) samples and any channel count,
 *  plus a resampler to the device rate. */
#pragma once
#include <string>
#include <vector>

namespace chompi_sim
{

struct WavClip
{
    int                rate     = 0; /**< sample rate of `left` / `right` */
    int                channels = 0; /**< channels in the file */
    std::vector<float> left, right;  /**< the first two channels; a mono file fills both */
};

/** Reads `path` into `out`. On failure returns false and sets `err`. */
bool LoadWav(const std::string& path, WavClip& out, std::string& err);

/** Resamples the clip to `rate` with 4-point Hermite interpolation. */
void ResampleWav(WavClip& clip, int rate);

} // namespace chompi_sim
