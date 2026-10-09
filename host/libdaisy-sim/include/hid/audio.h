#ifndef DSY_AUDIO_H
#define DSY_AUDIO_H
#include "per/sai.h"
namespace daisy
{
/** Audio engine of the simulated device. The registered callback is invoked by
 *  Sim::RenderBlock() with 4 input and 4 output channels (two SAIs). */
class AudioHandle
{
  public:
    struct Config
    {
        size_t                        blocksize;
        SaiHandle::Config::SampleRate samplerate;
        float                         postgain;
        float                         output_compensation;
        Config() : blocksize(48), samplerate(SaiHandle::Config::SampleRate::SAI_48KHZ), postgain(1.f), output_compensation(1.f) {}
    };
    enum class Result { OK, ERR };
    typedef const float* const* InputBuffer;
    typedef float**             OutputBuffer;
    typedef void (*AudioCallback)(InputBuffer in, OutputBuffer out, size_t size);
    typedef const float* InterleavingInputBuffer;
    typedef float*       InterleavingOutputBuffer;
    typedef void (*InterleavingAudioCallback)(InterleavingInputBuffer in, InterleavingOutputBuffer out, size_t size);

    AudioHandle() : pimpl_(nullptr) {}
    ~AudioHandle() {}
    AudioHandle(const AudioHandle& other) = default;
    AudioHandle& operator=(const AudioHandle& other) = default;

    Result        Init(const Config& config, SaiHandle sai);
    Result        Init(const Config& config, SaiHandle sai1, SaiHandle sai2);
    Result        DeInit();
    const Config& GetConfig() const;
    size_t        GetChannels() const;
    float         GetSampleRate();
    Result        SetSampleRate(SaiHandle::Config::SampleRate samplerate);
    Result        SetBlockSize(size_t size);
    Result        SetPostGain(float val);
    Result        SetOutputCompensation(float val);
    Result        Start(AudioCallback callback);
    Result        Start(InterleavingAudioCallback callback);
    Result        Stop();
    Result        ChangeCallback(AudioCallback callback);
    Result        ChangeCallback(InterleavingAudioCallback callback);

    class Impl;

  private:
    Impl* pimpl_;
};
} // namespace daisy
#endif
