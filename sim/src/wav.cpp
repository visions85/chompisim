/** @file wav.cpp
 *  @brief WAV reading and resampling for the audio-input feed. */
#include "wav.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

namespace chompi_sim
{
namespace
{

uint32_t U32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint16_t U16(const uint8_t* p)
{
    return uint16_t(p[0] | p[1] << 8);
}

/** One sample as a float in -1..1. `format` 1 = PCM, 3 = IEEE float. */
float Sample(const uint8_t* p, int bits, int format)
{
    switch(bits)
    {
        case 8: return (int(p[0]) - 128) / 128.f;
        case 16: return int16_t(U16(p)) / 32768.f;
        case 24: return float(int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) / 256) / 8388608.f;
        case 32:
            if(format == 3)
            {
                float f;
                std::memcpy(&f, p, 4);
                return f;
            }
            return float(int32_t(U32(p)) / 2147483648.0);
        case 64:
        {
            double d;
            std::memcpy(&d, p, 8);
            return float(d);
        }
        default: return 0.f;
    }
}

} // namespace

bool LoadWav(const std::string& path, WavClip& out, std::string& err)
{
    std::ifstream f(path, std::ios::binary);
    if(!f)
    {
        err = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if(d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
    {
        err = path + " is not a WAV file";
        return false;
    }
    int            format = 0, channels = 0, rate = 0, bits = 0;
    const uint8_t* data     = nullptr;
    size_t         data_len = 0;
    for(size_t pos = 12; pos + 8 <= d.size();)
    {
        const uint8_t* ch    = d.data() + pos;
        const uint8_t* body  = ch + 8;
        size_t         avail = d.size() - (pos + 8);
        size_t         len   = std::min(size_t(U32(ch + 4)), avail); // a truncated file yields what is there
        if(std::memcmp(ch, "fmt ", 4) == 0 && len >= 16)
        {
            format   = U16(body);
            channels = U16(body + 2);
            rate     = int(U32(body + 4));
            bits     = U16(body + 14);
            if(format == 0xFFFE && len >= 26) // WAVE_FORMAT_EXTENSIBLE: the sub-format GUID starts with the tag
                format = U16(body + 24);
        }
        else if(std::memcmp(ch, "data", 4) == 0)
        {
            data     = body;
            data_len = len;
        }
        pos += 8 + len + (len & 1);
    }
    if(!data || channels <= 0 || rate <= 0 || bits <= 0)
    {
        err = path + ": no usable fmt and data chunks";
        return false;
    }
    const bool ok = (format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32))
                    || (format == 3 && (bits == 32 || bits == 64));
    if(!ok)
    {
        err = path + ": unsupported sample format (tag " + std::to_string(format) + ", " + std::to_string(bits) + " bit)";
        return false;
    }
    const size_t bps = size_t(bits / 8), frame = bps * size_t(channels), frames = data_len / frame;
    out.rate     = rate;
    out.channels = channels;
    out.left.resize(frames);
    out.right.resize(frames);
    for(size_t i = 0; i < frames; i++)
    {
        const uint8_t* p = data + i * frame;
        out.left[i]      = Sample(p, bits, format);
        out.right[i]     = channels > 1 ? Sample(p + bps, bits, format) : out.left[i];
    }
    return true;
}

void ResampleWav(WavClip& clip, int rate)
{
    if(clip.rate == rate || clip.rate <= 0 || rate <= 0 || clip.left.empty())
    {
        clip.rate = rate;
        return;
    }
    auto resample = [&](const std::vector<float>& in) {
        const double       step = double(clip.rate) / rate;
        const size_t       n    = size_t(std::floor(double(in.size()) * rate / clip.rate));
        std::vector<float> out(n);
        auto at = [&](long i) { return i < 0 ? in.front() : (size_t(i) >= in.size() ? in.back() : in[size_t(i)]); };
        for(size_t k = 0; k < n; k++)
        {
            const double pos = double(k) * step;
            const long   i   = long(std::floor(pos));
            const float  t   = float(pos - double(i));
            const float  y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
            // 4-point, 3rd-order Hermite
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            out[k]         = ((c3 * t + c2) * t + c1) * t + y1;
        }
        return out;
    };
    clip.left  = resample(clip.left);
    clip.right = resample(clip.right);
    clip.rate  = rate;
}

} // namespace chompi_sim
