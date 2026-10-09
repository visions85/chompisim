#include "per/tim.h"
#include "per/tim_channel.h"
#include "daisy_seed.h"
#include "device.h"
using namespace daisy;
using chompi_sim::dev::Device;
using chompi_sim::dev::TimerModel;

class TimerHandle::Impl : public TimerModel
{
};

TimerHandle::Result TimerHandle::Init(const Config& config)
{
    Device& d          = Device::Get();
    pimpl_             = static_cast<Impl*>(d.GetTimer(int(config.periph)));
    pimpl_->period     = config.period;
    pimpl_->enable_irq = config.enable_irq;
    pimpl_->started    = false;
    return Result::OK;
}
TimerHandle::Result TimerHandle::DeInit()
{
    if(pimpl_)
        pimpl_->started = false;
    return Result::OK;
}
const TimerHandle::Config& TimerHandle::GetConfig() const
{
    static Config c;
    if(pimpl_)
    {
        c.periph     = Config::Peripheral(pimpl_->periph);
        c.period     = pimpl_->period;
        c.enable_irq = pimpl_->enable_irq;
    }
    return c;
}
TimerHandle::Result TimerHandle::SetPeriod(uint32_t ticks)
{
    if(!pimpl_)
        return Result::ERR;
    pimpl_->period = ticks;
    // keep the next tick within one new period, as the hardware auto-reload would
    double now = double(Device::Get().samples.load());
    double per = pimpl_->PeriodSamples();
    if(pimpl_->next_due > now + per)
        pimpl_->next_due = now + per;
    return Result::OK;
}
TimerHandle::Result TimerHandle::SetPrescaler(uint32_t val)
{
    if(!pimpl_)
        return Result::ERR;
    pimpl_->psc = val;
    return Result::OK;
}
TimerHandle::Result TimerHandle::Start()
{
    if(!pimpl_)
        return Result::ERR;
    pimpl_->started  = true;
    pimpl_->next_due = double(Device::Get().samples.load()) + pimpl_->PeriodSamples();
    return Result::OK;
}
TimerHandle::Result TimerHandle::Stop()
{
    if(pimpl_)
        pimpl_->started = false;
    return Result::OK;
}
uint32_t TimerHandle::GetFreq() { return pimpl_ ? uint32_t(240000000.0 / (double(pimpl_->psc) + 1.0)) : 0; }
uint32_t TimerHandle::GetTick() { return uint32_t(uint64_t(Device::Get().NowUs()) * (GetFreq() / 1000000)); }
uint32_t TimerHandle::GetMs() { return Device::Get().NowMs(); }
uint32_t TimerHandle::GetUs() { return Device::Get().NowUs(); }
void     TimerHandle::DelayTick(uint32_t del) { (void)del; }
void     TimerHandle::DelayMs(uint32_t del) { System::Delay(del); }
void     TimerHandle::DelayUs(uint32_t del) { System::DelayUs(del); }
void     TimerHandle::SetCallback(PeriodElapsedCallback cb, void* data)
{
    if(!pimpl_)
        return;
    pimpl_->cb  = cb;
    pimpl_->ctx = data;
}

// ---- TimChannel: PWM + DMA output, used for the WS2812 LED chains ----
void TimChannel::Init(const Config& cfg) { cfg_ = cfg; }
void TimChannel::Start() {}
void TimChannel::Stop() {}
void TimChannel::SetPwm(uint32_t val) { (void)val; }
void TimChannel::StartDma(void* data, size_t size, EndTransmissionFunctionPtr callback, void* cb_context)
{
    // D18 / TIM3 CH2 drives the keybed chain, D16 / TIM5 CH4 the panel chain
    bool smt = cfg_.pin == seed::D18 || (cfg_.pin == Pin() && cfg_.chn == Config::Channel::TWO);
    chompi_sim::dev::Fn done;
    if(callback)
        done = [callback, cb_context] { callback(cb_context); };
    Device::Get().LedDmaStart(smt, static_cast<const uint32_t*>(data), size, std::move(done));
}
