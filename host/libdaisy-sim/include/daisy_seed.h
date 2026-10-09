/** Host simulator version of libDaisy's DaisySeed board support object. */
#pragma once
#ifndef DSY_SEED_H
#define DSY_SEED_H
#include "daisy.h"

namespace daisy
{
class DaisySeed
{
  public:
    DaisySeed() {}
    ~DaisySeed() {}

    void Configure() {}
    void Init(bool boost = false);
    void DeInit() {}
    void DelayMs(size_t del) { System::Delay(del); }
    static dsy_gpio_pin GetPin(uint8_t pin_idx);

    void StartAudio(AudioHandle::InterleavingAudioCallback cb);
    void StartAudio(AudioHandle::AudioCallback cb);
    void ChangeAudioCallback(AudioHandle::InterleavingAudioCallback cb);
    void ChangeAudioCallback(AudioHandle::AudioCallback cb);
    void StopAudio();
    void SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate);
    float AudioSampleRate();
    void SetAudioBlockSize(size_t blocksize);
    size_t AudioBlockSize();
    float AudioCallbackRate() const;
    const SaiHandle& AudioSaiHandle() const { return sai_1_handle_; }

    void SetLed(bool state) { (void)state; }
    void SetTestPoint(bool state) { (void)state; }

    template <typename... VA>
    static void Print(const char* format, VA... va)
    {
        Logger<LOGGER_INTERNAL>::Print(format, va...);
    }
    template <typename... VA>
    static void PrintLine(const char* format, VA... va)
    {
        Logger<LOGGER_INTERNAL>::PrintLine(format, va...);
    }
    static void StartLog(bool wait_for_pc = false) { (void)wait_for_pc; }

    AudioHandle audio_handle;
    System      system;

    enum class BoardVersion
    {
        DAISY_SEED,
        DAISY_SEED_1_1,
        DAISY_SEED_2_DFM,
    };
    BoardVersion CheckBoardVersion() { return BoardVersion::DAISY_SEED_2_DFM; }

  private:
    SaiHandle sai_1_handle_;
    float     callback_rate_ = 0.f;
};

namespace seed
{
    constexpr Pin D0  = Pin(PORTB, 12);
    constexpr Pin D1  = Pin(PORTC, 11);
    constexpr Pin D2  = Pin(PORTC, 10);
    constexpr Pin D3  = Pin(PORTC, 9);
    constexpr Pin D4  = Pin(PORTC, 8);
    constexpr Pin D5  = Pin(PORTD, 2);
    constexpr Pin D6  = Pin(PORTC, 12);
    constexpr Pin D7  = Pin(PORTG, 10);
    constexpr Pin D8  = Pin(PORTG, 11);
    constexpr Pin D9  = Pin(PORTB, 4);
    constexpr Pin D10 = Pin(PORTB, 5);
    constexpr Pin D11 = Pin(PORTB, 8);
    constexpr Pin D12 = Pin(PORTB, 9);
    constexpr Pin D13 = Pin(PORTB, 6);
    constexpr Pin D14 = Pin(PORTB, 7);
    constexpr Pin D15 = Pin(PORTC, 0);
    constexpr Pin D16 = Pin(PORTA, 3);
    constexpr Pin D17 = Pin(PORTB, 1);
    constexpr Pin D18 = Pin(PORTA, 7);
    constexpr Pin D19 = Pin(PORTA, 6);
    constexpr Pin D20 = Pin(PORTC, 1);
    constexpr Pin D21 = Pin(PORTC, 4);
    constexpr Pin D22 = Pin(PORTA, 5);
    constexpr Pin D23 = Pin(PORTA, 4);
    constexpr Pin D24 = Pin(PORTA, 1);
    constexpr Pin D25 = Pin(PORTA, 0);
    constexpr Pin D26 = Pin(PORTD, 11);
    constexpr Pin D27 = Pin(PORTG, 9);
    constexpr Pin D28 = Pin(PORTA, 2);
    constexpr Pin D29 = Pin(PORTB, 14);
    constexpr Pin D30 = Pin(PORTB, 15);
    constexpr Pin A0  = D15;
    constexpr Pin A1  = D16;
    constexpr Pin A2  = D17;
    constexpr Pin A3  = D18;
    constexpr Pin A4  = D19;
    constexpr Pin A5  = D20;
    constexpr Pin A6  = D21;
    constexpr Pin A7  = D22;
    constexpr Pin A8  = D23;
    constexpr Pin A9  = D24;
    constexpr Pin A10 = D25;
    constexpr Pin A11 = D28;
    constexpr Pin D31 = Pin(PORTC, 2);
    constexpr Pin D32 = Pin(PORTC, 3);
    constexpr Pin A12 = D31;
    constexpr Pin A13 = D32;
} // namespace seed
} // namespace daisy

/** STM32 HAL low-power entry used by CHOMPI's battery lockout; a sleep on the host. */
#define PWR_LOWPOWERREGULATOR_ON 1
#define PWR_STOPENTRY_WFI 1
void HAL_PWR_EnterSTOPMode(uint32_t regulator, uint8_t stop_entry);
#endif
