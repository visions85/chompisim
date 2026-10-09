#pragma once
#ifndef DSY_SAI_H
#define DSY_SAI_H
#include "daisy_core.h"
namespace daisy
{
/** Serial audio interface configuration. On the host only the sample rate matters. */
class SaiHandle
{
  public:
    struct Config
    {
        enum class Peripheral { SAI_1, SAI_2 };
        enum class SampleRate { SAI_8KHZ, SAI_16KHZ, SAI_32KHZ, SAI_48KHZ, SAI_96KHZ };
        enum class BitDepth { SAI_16BIT, SAI_24BIT, SAI_32BIT };
        enum class Sync { MASTER, SLAVE };
        enum class Direction { TRANSMIT, RECEIVE };
        Peripheral periph;
        struct
        {
            dsy_gpio_pin mclk, fs, sck, sa, sb;
        } pin_config;
        SampleRate sr;
        BitDepth   bit_depth;
        Sync       a_sync, b_sync;
        Direction  a_dir, b_dir;
    };
    enum class Result { OK, ERR };
    typedef void (*CallbackFunctionPtr)(int32_t* in, int32_t* out, size_t size);

    SaiHandle() : pimpl_(nullptr) {}
    SaiHandle(const SaiHandle& other) = default;
    SaiHandle& operator=(const SaiHandle& other) = default;

    Result        Init(const Config& config);
    Result        DeInit();
    const Config& GetConfig() const;
    Result        StartDma(int32_t* buffer_rx, int32_t* buffer_tx, size_t size, CallbackFunctionPtr callback);
    Result        StopDma();
    float         GetSampleRate();
    size_t        GetBlockSize();
    float         GetBlockRate();
    size_t        GetOffset() const;
    bool          IsInitialized() const { return pimpl_ != nullptr; }

    class Impl;

  private:
    Impl* pimpl_;
};
} // namespace daisy
#endif
