#pragma once
#ifndef DSY_UART_H
#define DSY_UART_H
#include "daisy_core.h"
namespace daisy
{
/** UART on the simulated device. USART1 is CHOMPI's TRS MIDI port: transmitted
 *  bytes go to the simulator's MIDI-out buffer (end callbacks fire after the
 *  real 31250 baud transfer time) and MIDI-in bytes arrive through the DMA
 *  listen callback. */
class UartHandler
{
  public:
    struct Config
    {
        enum class Peripheral { USART_1 = 0, USART_2, USART_3, UART_4, UART_5, USART_6, UART_7, UART_8, LPUART_1 };
        enum class StopBits { BITS_0_5, BITS_1, BITS_1_5, BITS_2 };
        enum class Parity { NONE, EVEN, ODD };
        enum class Mode { RX, TX, TX_RX };
        enum class WordLength { BITS_7, BITS_8, BITS_9 };
        struct
        {
            dsy_gpio_pin tx;
            dsy_gpio_pin rx;
        } pin_config;
        Config()
        : periph(Peripheral::USART_1), stopbits(StopBits::BITS_1), parity(Parity::NONE), mode(Mode::TX_RX), wordlength(WordLength::BITS_8), baudrate(4800)
        {
        }
        Peripheral periph;
        StopBits   stopbits;
        Parity     parity;
        Mode       mode;
        WordLength wordlength;
        uint32_t   baudrate;
    };
    enum class Result { OK, ERR };
    enum class DmaDirection { RX, TX };
    typedef void (*StartCallbackFunctionPtr)(void* context);
    typedef void (*EndCallbackFunctionPtr)(void* context, Result result);
    typedef void (*CircularRxCallbackFunctionPtr)(uint8_t* data, size_t size, void* context, Result result);

    UartHandler() : pimpl_(nullptr) {}
    UartHandler(const UartHandler& other) = default;
    UartHandler& operator=(const UartHandler& other) = default;

    Result        Init(const Config& config);
    const Config& GetConfig() const;
    Result BlockingTransmit(uint8_t* buff, size_t size, uint32_t timeout = 100);
    Result BlockingReceive(uint8_t* buff, uint16_t size, uint32_t timeout = 100);
    Result DmaTransmit(uint8_t* buff, size_t size, StartCallbackFunctionPtr start_callback, EndCallbackFunctionPtr end_callback, void* callback_context);
    Result DmaReceive(uint8_t* buff, size_t size, StartCallbackFunctionPtr start_callback, EndCallbackFunctionPtr end_callback, void* callback_context);
    Result DmaListenStart(uint8_t* buff, size_t size, CircularRxCallbackFunctionPtr cb, void* callback_context);
    Result DmaListenStop();
    bool   IsListening() const;
    int    CheckError();
    int    PollReceive(uint8_t* buff, size_t size, uint32_t timeout);
    Result PollTx(uint8_t* buff, size_t size);

    class Impl;

  private:
    Impl* pimpl_;
};
} // namespace daisy
#endif
