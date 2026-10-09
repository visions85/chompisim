#pragma once
#ifndef __DSY_MIDIUSBTRANSPORT_H__
#define __DSY_MIDIUSBTRANSPORT_H__
#include <cstddef>
#include <cstdint>
namespace daisy
{
/** USB MIDI transport on the simulated device: a second virtual MIDI port. */
class MidiUsbTransport
{
  public:
    typedef void (*MidiRxParseCallback)(uint8_t* data, size_t size, void* context);
    struct Config
    {
        enum Periph { INTERNAL = 0, EXTERNAL };
        Periph  periph;
        uint8_t tx_retry_count;
        Config() : periph(INTERNAL), tx_retry_count(3) {}
    };
    void Init(Config config);
    void Reset();
    void StartRx(MidiRxParseCallback callback, void* context);
    bool RxActive();
    void FlushRx();
    bool Tx(uint8_t* buffer, size_t size);

    class Impl;
    MidiUsbTransport() : pimpl_(nullptr) {}
    ~MidiUsbTransport() {}
    MidiUsbTransport(const MidiUsbTransport& other) = default;
    MidiUsbTransport& operator=(const MidiUsbTransport& other) = default;

  private:
    Impl* pimpl_;
};
} // namespace daisy
#endif
