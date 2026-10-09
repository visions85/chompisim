#include "hid/midi.h"
#include "device.h"
using namespace daisy;
using chompi_sim::dev::Device;

namespace daisy
{
static constexpr size_t kDefaultMidiRxBufferSize = 256;
static uint8_t          default_midi_rx_buffer[kDefaultMidiRxBufferSize];

MidiUartTransport::Config::Config()
{
    periph         = UartHandler::Config::Peripheral::USART_1;
    rx             = {DSY_GPIOB, 7};
    tx             = {DSY_GPIOB, 6};
    rx_buffer      = default_midi_rx_buffer;
    rx_buffer_size = kDefaultMidiRxBufferSize;
}
} // namespace daisy

// ---- USB MIDI transport ----
class MidiUsbTransport::Impl
{
  public:
    Config cfg;
};
void MidiUsbTransport::Init(Config config)
{
    static Impl impl;
    pimpl_      = &impl;
    pimpl_->cfg = config;
}
void MidiUsbTransport::Reset() {}
void MidiUsbTransport::StartRx(MidiRxParseCallback callback, void* context)
{
    Device& d       = Device::Get();
    d.usb_rx_cb     = callback;
    d.usb_rx_ctx    = context;
    d.usb_listening = true;
}
bool MidiUsbTransport::RxActive() { return Device::Get().usb_listening; }
void MidiUsbTransport::FlushRx() {}
bool MidiUsbTransport::Tx(uint8_t* buffer, size_t size)
{
    Device& d = Device::Get();
    std::lock_guard<std::mutex> l(d.midi_m);
    d.usb_out.insert(d.usb_out.end(), buffer, buffer + size);
    return true;
}
