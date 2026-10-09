#pragma once
#ifndef DSY_I2C_H
#define DSY_I2C_H
#include "daisy_core.h"
namespace daisy
{
/** I2C master on the simulated device. The only slave CHOMPI talks to is the
 *  MP2722 battery charger, which the simulator models (see sim/src/device.cpp). */
class I2CHandle
{
  public:
    struct Config
    {
        enum class Mode { I2C_MASTER, I2C_SLAVE };
        enum class Peripheral { I2C_1 = 0, I2C_2, I2C_3, I2C_4 };
        enum class Speed { I2C_100KHZ, I2C_400KHZ, I2C_1MHZ };
        Peripheral periph;
        struct
        {
            dsy_gpio_pin scl;
            dsy_gpio_pin sda;
        } pin_config;
        Speed   speed;
        Mode    mode;
        uint8_t address;
    };
    enum class Result { OK, ERR };
    enum class Direction { TRANSMIT, RECEIVE };
    typedef void (*CallbackFunctionPtr)(void* context, Result result);

    I2CHandle() : pimpl_(nullptr) {}
    I2CHandle(const I2CHandle& other) = default;
    I2CHandle& operator=(const I2CHandle& other) = default;

    Result        Init(const Config& config);
    const Config& GetConfig() const;
    Result TransmitBlocking(uint16_t address, uint8_t* data, uint16_t size, uint32_t timeout);
    Result ReceiveBlocking(uint16_t address, uint8_t* data, uint16_t size, uint32_t timeout);
    Result TransmitDma(uint16_t address, uint8_t* data, uint16_t size, CallbackFunctionPtr callback, void* callback_context);
    Result ReceiveDma(uint16_t address, uint8_t* data, uint16_t size, CallbackFunctionPtr callback, void* callback_context);
    Result ReadDataAtAddress(uint16_t address, uint16_t mem_address, uint16_t mem_address_size, uint8_t* data, uint16_t data_size, uint32_t timeout);
    Result WriteDataAtAddress(uint16_t address, uint16_t mem_address, uint16_t mem_address_size, uint8_t* data, uint16_t data_size, uint32_t timeout);

    class Impl;

  private:
    Impl* pimpl_;
};
} // namespace daisy
#endif
