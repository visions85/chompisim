/** @file device.h
 *  @brief Internal model of the simulated CHOMPI hardware. The libDaisy shim
 *  (host/libdaisy-sim) and the public Sim facade both talk to this object.
 */
#pragma once
#include "chompi_sim/sim.h"
#include "hid/audio.h"
#include "hid/usb_midi.h"
#include "per/tim.h"
#include "per/uart.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace chompi_sim
{
namespace dev
{

using Fn = std::function<void()>;

/** One-shot event on the sample clock (DMA completions, deferred callbacks). */
struct ScheduledEvent
{
    uint64_t due;
    uint64_t seq;
    Fn       fn;
    bool     operator>(const ScheduledEvent& o) const { return due != o.due ? due > o.due : seq > o.seq; }
};

/** Chain of CD4021 parallel-in / serial-out shift registers, driven bit-serially
 *  through the clock / latch / data GPIO pins exactly like the hardware. */
struct Chain4021
{
    int                      nbits = 0;
    std::function<bool(int)> bit; /**< current level of parallel input `i` (true = high) */
    std::atomic<int>         pos{0};
    std::atomic<bool>        clk{false}, latch{false};
    std::array<std::atomic<bool>, 64> snapshot{};

    void OnLatch(bool v)
    {
        if(v && !latch)
        {
            for(int i = 0; i < nbits; i++)
                snapshot[i] = bit ? bit(i) : true;
            pos = 0;
        }
        latch = v;
    }
    void OnClk(bool v)
    {
        if(v && !clk)
            pos++;
        clk = v;
    }
    bool Data() const
    {
        int idx = nbits - 1 - pos;
        return (idx >= 0 && idx < nbits) ? snapshot[idx].load() : true;
    }
};

/** Quadrature encoder: queued detents are played out as A/B phase sequences
 *  slow enough for the firmware's 1 kHz debouncer to see every phase. */
struct EncoderModel
{
    std::atomic<int>  pending{0};
    std::atomic<bool> pressed{false};
    std::atomic<bool> a{true}, b{true};
    int               dir   = 0;
    int               phase = -1; /**< -1 idle, else index into the phase table */
    uint32_t          phase_started_ms = 0;
    void              Tick(uint32_t now_ms);
};

/** A hardware timer (TIM2..TIM5, TIM16). */
struct TimerModel
{
    int                                   periph     = 0;
    bool                                  started    = false;
    bool                                  enable_irq = false;
    uint32_t                              psc        = 0;
    uint32_t                              period     = 0xffffffff;
    daisy::TimerHandle::PeriodElapsedCallback cb     = nullptr;
    void*                                 ctx        = nullptr;
    double                                next_due   = 0; /**< in samples */
    double PeriodSamples() const
    {
        // Timer kernel clock on the H750 in boost mode: 2 x PCLK = 240 MHz.
        return double(uint64_t(psc) + 1) * double(uint64_t(period) + 1) / 240.0e6 * double(kSampleRate);
    }
};

struct PinCell
{
    std::atomic<bool>         level{true};
    std::function<bool()>     read;  /**< optional model hook, overrides `level` */
    std::function<void(bool)> write; /**< optional model hook, sees writes */
};

class Device
{
  public:
    static Device& Get();

    // ---- lifecycle ----
    bool  MapSdram();
    void* sdram = nullptr; /**< 64 MB mapped at 0xC0000000, see MapSdram() */
    bool  Init(const Config& cfg);
    void Start();
    void Stop();
    bool initialised = false;
    Config cfg;
    std::atomic<bool> fw_should_run{false};
    std::atomic<bool> fw_alive{false};
    std::thread       fw_thread;

    // ---- time ----
    std::atomic<uint64_t> samples{0};
    std::chrono::steady_clock::time_point wall_epoch;
    uint32_t NowMs() const;
    uint32_t NowUs() const;
    bool     Lockstep() const { return !cfg.realtime; }

    // lockstep baton between the audio thread and the firmware thread
    std::mutex              ls_m;
    std::condition_variable ls_cv;
    bool                    fw_turn = false;
    void                    FwWaitTurn();
    void                    FwYield();
    void                    HandBaton();
    uint32_t                fw_getnow_budget = 0;
    uint32_t                fw_delay_debt_us = 0;
    void                    IsrDelayUs(uint64_t us);
    void                    Delay(uint32_t ms);
    void                    DelayUs(uint32_t us);
    void                    OnGetNow();

    // ---- "interrupt" lock ----
    std::recursive_mutex irq_mutex;

    // ---- pins ----
    static constexpr int kPorts = 12, kPinsPerPort = 16;
    PinCell pins[kPorts][kPinsPerPort];
    bool    ReadPin(int port, int pin);
    void    WritePin(int port, int pin, bool v);

    // ---- panel controls ----
    std::array<std::atomic<bool>, NUM_BUTTONS> button_pressed{};
    std::atomic<bool>                        toggle_down{false};
    Chain4021                                button_chain, encoder_chain;
    EncoderModel                             enc[kNumEncoders];
    void                                     TickModels(uint32_t now_ms);

    // ---- scheduler ----
    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, std::greater<ScheduledEvent>> events;
    uint64_t                 event_seq = 0;
    std::mutex               ev_m;
    void                     ScheduleIn(double seconds, Fn fn);
    std::vector<TimerModel*> timers;
    TimerModel*              GetTimer(int periph);
    void                     RunDue(uint64_t now);
    void                     RunOneShotsDue(uint64_t now);

    // ---- audio ----
    daisy::AudioHandle::AudioCallback             audio_cb    = nullptr;
    daisy::AudioHandle::InterleavingAudioCallback audio_cb_il = nullptr;
    size_t                                        blocksize   = kBlockSize;
    int                                           num_channels = kNumOutputs;
    void RenderBlock(const float* const* in, float* const* out);
    void RenderStereo(float* interleaved, size_t frames, int pair);
    std::mutex         render_m;
    std::vector<float> carry;
    size_t             carry_pos = 0;
    std::thread        null_thread;
    std::atomic<bool>  null_run{false};

    // ---- LEDs ----
    mutable std::mutex led_m;
    Rgb                key_leds[kNumKeyLeds];
    Rgb                panel_leds[kNumPanelLeds];
    void               LedDmaStart(bool smt_chain, const uint32_t* data, size_t n);
    void               LedDmaStart(bool smt_chain, const uint32_t* data, size_t n, Fn done);

    // ---- MIDI ----
    std::mutex           midi_m;
    std::vector<uint8_t> midi_out, usb_out, midi_in_pending, usb_in_pending;
    uint8_t*             uart_rx_buf  = nullptr;
    size_t               uart_rx_size = 0;
    daisy::UartHandler::CircularRxCallbackFunctionPtr uart_rx_cb = nullptr;
    void*                uart_rx_ctx  = nullptr;
    std::atomic<bool>    uart_listening{false};
    daisy::MidiUsbTransport::MidiRxParseCallback usb_rx_cb = nullptr;
    void*                usb_rx_ctx = nullptr;
    std::atomic<bool>    usb_listening{false};
    void                 DeliverMidiIn();

    // ---- MP2722 charger ----
    std::mutex        mp_m;
    uint8_t           mp_regs[256] = {};
    uint8_t           mp_ptr       = 0;
    std::atomic<bool> usb_power{true};
    std::atomic<bool> batt_low{false};
    void              MpTransmit(const uint8_t* data, size_t n);
    void              MpReceive(uint8_t* data, size_t n);

    // ---- log / stats ----
    std::mutex               log_m;
    std::vector<std::string> log;
    std::string              log_partial;
    void                     Log(const char* text, bool newline);
    Stats                    stats;
};

/** Set by the firmware thread so the shim can tell which context it runs in. */
extern thread_local bool t_in_firmware_thread;
/** Nesting depth of __disable_irq / ScopedIrqBlocker on the current thread. */
extern thread_local int t_irq_depth;

} // namespace dev
} // namespace chompi_sim
