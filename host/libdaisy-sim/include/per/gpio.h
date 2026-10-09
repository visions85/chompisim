#pragma once
#ifndef DSY_GPIO_H
#define DSY_GPIO_H
#include "daisy_core.h"

#ifdef __cplusplus
namespace daisy
{
/** GPIO on the simulated device: every pin is a bit in a pin table the device
 *  models (shift registers, encoders, switches) can hook. */
class GPIO
{
  public:
    enum class Mode { INPUT, OUTPUT, OUTPUT_OD, ANALOG };
    enum class Pull { NOPULL, PULLUP, PULLDOWN };
    enum class Speed { LOW, MEDIUM, HIGH, VERY_HIGH };
    struct Config
    {
        Pin   pin;
        Mode  mode;
        Pull  pull;
        Speed speed;
        Config() : pin(), mode(Mode::INPUT), pull(Pull::NOPULL), speed(Speed::LOW) {}
    };
    GPIO() {}
    void    Init(const Config& cfg);
    void    Init(Pin p, const Config& cfg);
    void    Init(Pin p, Mode m = Mode::INPUT, Pull pu = Pull::NOPULL, Speed sp = Speed::LOW);
    void    DeInit() {}
    bool    Read();
    void    Write(bool state);
    void    Toggle();
    Config& GetConfig() { return cfg_; }

  private:
    Config cfg_;
};
} // namespace daisy

extern "C"
{
#endif
    typedef enum { DSY_GPIO_MODE_INPUT, DSY_GPIO_MODE_OUTPUT_PP, DSY_GPIO_MODE_OUTPUT_OD, DSY_GPIO_MODE_ANALOG, DSY_GPIO_MODE_LAST } dsy_gpio_mode;
    typedef enum { DSY_GPIO_NOPULL, DSY_GPIO_PULLUP, DSY_GPIO_PULLDOWN } dsy_gpio_pull;
    typedef struct
    {
        dsy_gpio_pin  pin;
        dsy_gpio_mode mode;
        dsy_gpio_pull pull;
    } dsy_gpio;
    void    dsy_gpio_init(const dsy_gpio* p);
    void    dsy_gpio_deinit(const dsy_gpio* p);
    uint8_t dsy_gpio_read(const dsy_gpio* p);
    void    dsy_gpio_write(const dsy_gpio* p, uint8_t state);
    void    dsy_gpio_toggle(const dsy_gpio* p);
#ifdef __cplusplus
}
#endif
#endif
