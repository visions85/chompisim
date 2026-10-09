#include "per/uart.h"
#include "device.h"
using namespace daisy;
using chompi_sim::dev::Device;

class UartHandler::Impl
{
  public:
    Config cfg;
    bool   tx_busy = false;
};

UartHandler::Result UartHandler::Init(const Config& config)
{
    static Impl impls[9];
    pimpl_      = &impls[int(config.periph) % 9];
    pimpl_->cfg = config;
    return Result::OK;
}
const UartHandler::Config& UartHandler::GetConfig() const
{
    static Config c;
    return pimpl_ ? pimpl_->cfg : c;
}
UartHandler::Result UartHandler::BlockingTransmit(uint8_t* buff, size_t size, uint32_t timeout)
{
    (void)timeout;
    return PollTx(buff, size);
}
UartHandler::Result UartHandler::BlockingReceive(uint8_t*, uint16_t, uint32_t) { return Result::ERR; }

UartHandler::Result UartHandler::DmaTransmit(uint8_t* buff, size_t size, StartCallbackFunctionPtr start_callback, EndCallbackFunctionPtr end_callback, void* callback_context)
{
    Device& d = Device::Get();
    if(start_callback)
        start_callback(callback_context);
    {
        std::lock_guard<std::mutex> l(d.midi_m);
        d.midi_out.insert(d.midi_out.end(), buff, buff + size);
        d.stats.midi_bytes_out += size;
    }
    // 10 bits per byte at 31250 baud = 320 us per byte
    double seconds = double(size) * 10.0 / double(pimpl_ ? pimpl_->cfg.baudrate : 31250);
    if(end_callback)
        d.ScheduleIn(seconds, [end_callback, callback_context] { end_callback(callback_context, Result::OK); });
    return Result::OK;
}
UartHandler::Result UartHandler::DmaReceive(uint8_t*, size_t, StartCallbackFunctionPtr, EndCallbackFunctionPtr, void*) { return Result::ERR; }

UartHandler::Result UartHandler::DmaListenStart(uint8_t* buff, size_t size, CircularRxCallbackFunctionPtr cb, void* callback_context)
{
    Device& d       = Device::Get();
    d.uart_rx_buf   = buff;
    d.uart_rx_size  = size;
    d.uart_rx_cb    = cb;
    d.uart_rx_ctx   = callback_context;
    d.uart_listening = true;
    return Result::OK;
}
UartHandler::Result UartHandler::DmaListenStop()
{
    Device::Get().uart_listening = false;
    return Result::OK;
}
bool UartHandler::IsListening() const { return Device::Get().uart_listening; }
int  UartHandler::CheckError() { return 0; }
int  UartHandler::PollReceive(uint8_t*, size_t, uint32_t) { return 0; }
UartHandler::Result UartHandler::PollTx(uint8_t* buff, size_t size)
{
    Device& d = Device::Get();
    std::lock_guard<std::mutex> l(d.midi_m);
    d.midi_out.insert(d.midi_out.end(), buff, buff + size);
    d.stats.midi_bytes_out += size;
    return Result::OK;
}
