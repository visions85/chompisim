#pragma once
#ifndef DSY_TIM_CHANNEL_H
#define DSY_TIM_CHANNEL_H
#include "daisy_core.h"
#include "tim.h"
namespace daisy
{
/** Timer output channel. CHOMPI drives its WS2812 LED chains with PWM + DMA
 *  through this class; the simulator decodes the DMA pulse-width buffer back
 *  into LED colours and fires the end-of-transfer callback after the time the
 *  real transfer would take. */
class TimChannel
{
  public:
    struct Config
    {
        enum class Channel { ONE, TWO, THREE, FOUR };
        enum class Mode { INPUT_CAPTURE, OUTPUT_COMPARE, PWM, ONE_PULSE };
        enum class Polarity { HIGH, LOW };
        TimerHandle* tim;
        Channel      chn;
        Mode         mode;
        Polarity     polarity;
        Pin          pin;
        Config() : tim(nullptr), chn(Channel::ONE), mode(Mode::PWM), polarity(Polarity::LOW) {}
    };
    TimChannel() {}
    ~TimChannel() {}
    void Init(const Config& cfg);
    void Start();
    void Stop();
    void SetPwm(uint32_t val);
    typedef void (*EndTransmissionFunctionPtr)(void* context);
    void StartDma(void* data, size_t size, EndTransmissionFunctionPtr callback = nullptr, void* cb_context = nullptr);
    const Config& GetConfig() const { return cfg_; }

  private:
    Config cfg_;
};
} // namespace daisy
#endif
