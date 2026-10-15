/** @file shimmer.h
 *  @brief The shimmer: an octave-up pitch shifter for the reverb's feedback (DRONE).
 *
 *  Two taps read a short delay line twice as fast as it is written, each
 *  faded in and out with a raised cosine so the splices do not click; the
 *  taps are half a window apart and their windows sum to one. The buffer
 *  is the caller's (SDRAM on the instrument).
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chompi
{
class OctaveShifter
{
public:
    static constexpr int kSize = 8192; /**< the window: 171 ms at 48 kHz, splices at 5.9 Hz */

    void Init(float* buffer)
    {
        buf_ = buffer;
        std::fill(buf_, buf_ + kSize, 0.f);
        for(int i = 0; i < kWin; i++)
        {
            const float s = sinf(3.14159265f * (float(i) + .5f) / float(kWin));
            win_[i]       = s * s;
        }
        w_ = phase_ = 0;
    }

    float Process(float in)
    {
        buf_[w_] = in;
        // tap A lags the write head by kSize - phase samples, tap B by half a window less;
        // every lag shrinks by one sample per sample, so the taps read at twice the speed
        const int lag_a  = kSize - phase_;
        int       lag_b  = lag_a - kSize / 2;
        int       phase_b = phase_ + kSize / 2;
        if(lag_b <= 0)
            lag_b += kSize;
        if(phase_b >= kSize)
            phase_b -= kSize;
        const float out = Read(lag_a) * win_[phase_ * kWin / kSize] + Read(lag_b) * win_[phase_b * kWin / kSize];
        if(++w_ >= kSize)
            w_ = 0;
        if(++phase_ >= kSize)
            phase_ = 0;
        return out;
    }

private:
    static constexpr int kWin = 1024;

    float Read(int lag) const
    {
        int i = w_ - lag;
        if(i < 0)
            i += kSize;
        return buf_[i];
    }

    float* buf_   = nullptr;
    float  win_[kWin];
    int    w_     = 0;
    int    phase_ = 0;
};
} // namespace chompi
