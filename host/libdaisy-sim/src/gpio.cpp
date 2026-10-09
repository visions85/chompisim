#include "per/gpio.h"
#include "device.h"
using namespace daisy;
using chompi_sim::dev::Device;

void GPIO::Init(const Config& cfg) { cfg_ = cfg; }
void GPIO::Init(Pin p, const Config& cfg)
{
    cfg_     = cfg;
    cfg_.pin = p;
}
void GPIO::Init(Pin p, Mode m, Pull pu, Speed sp)
{
    cfg_.pin   = p;
    cfg_.mode  = m;
    cfg_.pull  = pu;
    cfg_.speed = sp;
    if(cfg_.pin.IsValid() && cfg_.pull == Pull::PULLDOWN)
        Device::Get().pins[cfg_.pin.port][cfg_.pin.pin].level = false;
}
bool GPIO::Read()
{
    if(!cfg_.pin.IsValid())
        return cfg_.pull != Pull::PULLDOWN;
    return Device::Get().ReadPin(cfg_.pin.port, cfg_.pin.pin);
}
void GPIO::Write(bool state)
{
    if(!cfg_.pin.IsValid())
        return;
    Device::Get().WritePin(cfg_.pin.port, cfg_.pin.pin, state);
}
void GPIO::Toggle() { Write(!Read()); }

extern "C"
{
    void dsy_gpio_init(const dsy_gpio* p)
    {
        if(p->pin.port == DSY_GPIOX || p->pin.pin > 15)
            return;
        if(p->pull == DSY_GPIO_PULLDOWN)
            Device::Get().pins[p->pin.port][p->pin.pin].level = false;
    }
    void    dsy_gpio_deinit(const dsy_gpio* p) { (void)p; }
    uint8_t dsy_gpio_read(const dsy_gpio* p)
    {
        if(p->pin.port == DSY_GPIOX || p->pin.pin > 15)
            return p->pull == DSY_GPIO_PULLDOWN ? 0 : 1;
        return Device::Get().ReadPin(p->pin.port, p->pin.pin) ? 1 : 0;
    }
    void dsy_gpio_write(const dsy_gpio* p, uint8_t state)
    {
        if(p->pin.port == DSY_GPIOX || p->pin.pin > 15)
            return;
        Device::Get().WritePin(p->pin.port, p->pin.pin, state != 0);
    }
    void dsy_gpio_toggle(const dsy_gpio* p) { dsy_gpio_write(p, !dsy_gpio_read(p)); }
}
