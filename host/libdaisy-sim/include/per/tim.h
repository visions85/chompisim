#pragma once
#ifndef DSY_TIM_H
#define DSY_TIM_H
#include <cstdint>
namespace daisy
{
/** Hardware timer on the simulated device. Period-elapsed callbacks fire from
 *  the simulator's scheduler (inside Sim::RenderBlock) at
 *  240 MHz / ((prescaler + 1) * (period + 1)), as on the STM32H750. */
class TimerHandle
{
  public:
    struct Config
    {
        enum class Peripheral { TIM_2 = 0, TIM_3, TIM_4, TIM_5, TIM_16 };
        enum class CounterDir { UP = 0, DOWN };
        Peripheral periph;
        CounterDir dir;
        uint32_t   period;
        bool       enable_irq;
        Config() : periph(Peripheral::TIM_2), dir(CounterDir::UP), period(0xffffffff), enable_irq(false) {}
    };
    enum class Result { OK, ERR };
    typedef void (*PeriodElapsedCallback)(void* data);

    TimerHandle() : pimpl_(nullptr) {}
    TimerHandle(const TimerHandle& other) = default;
    TimerHandle& operator=(const TimerHandle& other) = default;
    ~TimerHandle() {}

    Result        Init(const Config& config);
    Result        DeInit();
    const Config& GetConfig() const;
    Result        SetPeriod(uint32_t ticks);
    Result        SetPrescaler(uint32_t val);
    Result        Start();
    Result        Stop();
    uint32_t      GetFreq();
    uint32_t      GetTick();
    uint32_t      GetMs();
    uint32_t      GetUs();
    void          DelayTick(uint32_t del);
    void          DelayMs(uint32_t del);
    void          DelayUs(uint32_t del);
    void          SetCallback(PeriodElapsedCallback cb, void* data = nullptr);

    class Impl;

  private:
    Impl* pimpl_;
};
} // namespace daisy
#endif
