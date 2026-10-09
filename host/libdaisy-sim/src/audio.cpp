#include "hid/audio.h"
#include "daisy_seed.h"
#include "device.h"
using namespace daisy;
using chompi_sim::dev::Device;

class AudioHandle::Impl
{
  public:
    Config config;
    int    channels = 0;
};

AudioHandle::Result AudioHandle::Init(const Config& config, SaiHandle sai)
{
    (void)sai;
    static Impl impl;
    pimpl_           = &impl;
    pimpl_->config   = config;
    pimpl_->channels = 2;
    Device::Get().blocksize = config.blocksize;
    return Result::OK;
}
AudioHandle::Result AudioHandle::Init(const Config& config, SaiHandle sai1, SaiHandle sai2)
{
    (void)sai2;
    Init(config, sai1);
    pimpl_->channels = 4;
    return Result::OK;
}
AudioHandle::Result AudioHandle::DeInit() { return Result::OK; }
const AudioHandle::Config& AudioHandle::GetConfig() const
{
    static Config c;
    return pimpl_ ? pimpl_->config : c;
}
size_t AudioHandle::GetChannels() const { return pimpl_ ? pimpl_->channels : 0; }
float  AudioHandle::GetSampleRate() { return float(chompi_sim::kSampleRate); }
AudioHandle::Result AudioHandle::SetSampleRate(SaiHandle::Config::SampleRate) { return Result::OK; }
AudioHandle::Result AudioHandle::SetBlockSize(size_t size)
{
    if(size != chompi_sim::kBlockSize)
        return Result::ERR; // the simulator renders fixed 24-sample blocks
    return Result::OK;
}
AudioHandle::Result AudioHandle::SetPostGain(float) { return Result::OK; }
AudioHandle::Result AudioHandle::SetOutputCompensation(float) { return Result::OK; }
AudioHandle::Result AudioHandle::Start(AudioCallback callback)
{
    Device::Get().audio_cb = callback;
    return Result::OK;
}
AudioHandle::Result AudioHandle::Start(InterleavingAudioCallback callback)
{
    Device::Get().audio_cb_il = callback;
    return Result::OK;
}
AudioHandle::Result AudioHandle::Stop()
{
    Device::Get().audio_cb = nullptr;
    return Result::OK;
}
AudioHandle::Result AudioHandle::ChangeCallback(AudioCallback callback) { return Start(callback); }
AudioHandle::Result AudioHandle::ChangeCallback(InterleavingAudioCallback callback) { return Start(callback); }

// ---- DaisySeed ----
void DaisySeed::Init(bool boost)
{
    (void)boost;
    SaiHandle::Config sai_cfg = {};
    sai_cfg.periph            = SaiHandle::Config::Peripheral::SAI_1;
    sai_cfg.sr                = SaiHandle::Config::SampleRate::SAI_48KHZ;
    sai_cfg.bit_depth         = SaiHandle::Config::BitDepth::SAI_24BIT;
    sai_1_handle_.Init(sai_cfg);
    AudioHandle::Config audio_cfg;
    audio_cfg.blocksize = 48;
    audio_handle.Init(audio_cfg, sai_1_handle_);
    callback_rate_ = 48000.f / 48.f;
}
dsy_gpio_pin DaisySeed::GetPin(uint8_t pin_idx)
{
    static const Pin table[33] = {seed::D0,  seed::D1,  seed::D2,  seed::D3,  seed::D4,  seed::D5,  seed::D6,  seed::D7,  seed::D8,
                                  seed::D9,  seed::D10, seed::D11, seed::D12, seed::D13, seed::D14, seed::D15, seed::D16, seed::D17,
                                  seed::D18, seed::D19, seed::D20, seed::D21, seed::D22, seed::D23, seed::D24, seed::D25, seed::D26,
                                  seed::D27, seed::D28, seed::D29, seed::D30, seed::D31, seed::D32};
    return pin_idx < 33 ? dsy_gpio_pin(table[pin_idx]) : dsy_gpio_pin(Pin());
}
void DaisySeed::StartAudio(AudioHandle::InterleavingAudioCallback cb) { audio_handle.Start(cb); }
void DaisySeed::StartAudio(AudioHandle::AudioCallback cb) { audio_handle.Start(cb); }
void DaisySeed::ChangeAudioCallback(AudioHandle::InterleavingAudioCallback cb) { audio_handle.ChangeCallback(cb); }
void DaisySeed::ChangeAudioCallback(AudioHandle::AudioCallback cb) { audio_handle.ChangeCallback(cb); }
void DaisySeed::StopAudio() { audio_handle.Stop(); }
void DaisySeed::SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate) { audio_handle.SetSampleRate(samplerate); }
float DaisySeed::AudioSampleRate() { return audio_handle.GetSampleRate(); }
void  DaisySeed::SetAudioBlockSize(size_t blocksize) { audio_handle.SetBlockSize(blocksize); }
size_t DaisySeed::AudioBlockSize() { return audio_handle.GetConfig().blocksize; }
float  DaisySeed::AudioCallbackRate() const { return 48000.f / float(audio_handle.GetConfig().blocksize); }

void HAL_PWR_EnterSTOPMode(uint32_t regulator, uint8_t stop_entry)
{
    (void)regulator;
    (void)stop_entry;
    System::Delay(100);
}
