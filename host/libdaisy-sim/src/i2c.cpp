#include "per/i2c.h"
#include "device.h"
using namespace daisy;
using chompi_sim::dev::Device;

class I2CHandle::Impl
{
  public:
    Config cfg;
};

I2CHandle::Result I2CHandle::Init(const Config& config)
{
    static Impl impls[4];
    pimpl_      = &impls[int(config.periph) & 3];
    pimpl_->cfg = config;
    return Result::OK;
}
const I2CHandle::Config& I2CHandle::GetConfig() const
{
    static Config c;
    return pimpl_ ? pimpl_->cfg : c;
}
I2CHandle::Result I2CHandle::TransmitBlocking(uint16_t address, uint8_t* data, uint16_t size, uint32_t timeout)
{
    (void)timeout;
    if((address & 0x7F) == 0x3F)
        Device::Get().MpTransmit(data, size);
    return Result::OK;
}
I2CHandle::Result I2CHandle::ReceiveBlocking(uint16_t address, uint8_t* data, uint16_t size, uint32_t timeout)
{
    (void)timeout;
    if((address & 0x7F) == 0x3F)
        Device::Get().MpReceive(data, size);
    return Result::OK;
}
I2CHandle::Result I2CHandle::TransmitDma(uint16_t address, uint8_t* data, uint16_t size, CallbackFunctionPtr callback, void* callback_context)
{
    Result r = TransmitBlocking(address, data, size, 0);
    if(callback)
        callback(callback_context, r);
    return r;
}
I2CHandle::Result I2CHandle::ReceiveDma(uint16_t address, uint8_t* data, uint16_t size, CallbackFunctionPtr callback, void* callback_context)
{
    Result r = ReceiveBlocking(address, data, size, 0);
    if(callback)
        callback(callback_context, r);
    return r;
}
I2CHandle::Result I2CHandle::ReadDataAtAddress(uint16_t address, uint16_t mem_address, uint16_t mem_address_size, uint8_t* data, uint16_t data_size, uint32_t timeout)
{
    (void)mem_address_size;
    uint8_t reg = uint8_t(mem_address);
    TransmitBlocking(address, &reg, 1, timeout);
    return ReceiveBlocking(address, data, data_size, timeout);
}
I2CHandle::Result I2CHandle::WriteDataAtAddress(uint16_t address, uint16_t mem_address, uint16_t mem_address_size, uint8_t* data, uint16_t data_size, uint32_t timeout)
{
    (void)mem_address_size;
    uint8_t buf[256];
    buf[0]     = uint8_t(mem_address);
    uint16_t n = data_size > 255 ? 255 : data_size;
    for(uint16_t i = 0; i < n; i++)
        buf[1 + i] = data[i];
    return TransmitBlocking(address, buf, uint16_t(n + 1), timeout);
}
