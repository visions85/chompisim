/** @file device.cpp
 *  @brief The simulated CHOMPI: pins, shift registers, encoders, timers, DMA,
 *  audio, MIDI, charger, time and the lockstep scheduler.
 */
#include "device.h"
#include "chompi_sim_hooks.h"
#include "daisy_seed.h"
#include "fatfs_host.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sys/mman.h>

namespace chompi_sim
{
namespace dev
{

thread_local bool t_in_firmware_thread = false;
thread_local int  t_irq_depth          = 0;

Device& Device::Get()
{
    static Device d;
    return d;
}

// ---------------------------------------------------------------------------
// Encoder phase sequences. Each phase is held >= 2 ms so the firmware's 1 kHz
// debouncer (shift-register variant needs A low for two samples) sees it.
// ---------------------------------------------------------------------------
namespace
{
const bool kCw[5][2]  = {{1, 0}, {0, 0}, {0, 0}, {1, 0}, {1, 1}};
const bool kCcw[5][2] = {{0, 1}, {0, 0}, {0, 0}, {0, 1}, {1, 1}};
constexpr uint32_t kPhaseHoldMs = 2;
} // namespace

void EncoderModel::Tick(uint32_t now_ms)
{
    if(phase < 0)
    {
        int p = pending.load();
        if(p == 0)
            return;
        dir = p > 0 ? 1 : -1;
        pending -= dir;
        phase            = 0;
        phase_started_ms = now_ms;
    }
    else if(now_ms - phase_started_ms >= kPhaseHoldMs)
    {
        phase++;
        phase_started_ms = now_ms;
        if(phase >= 5)
        {
            phase = -1;
            a     = true;
            b     = true;
            return;
        }
    }
    const bool(*tab)[2] = dir > 0 ? kCw : kCcw;
    a                   = tab[phase][0];
    b                   = tab[phase][1];
}

// ---------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------
bool Device::ReadPin(int port, int pin)
{
    if(port < 0 || port >= kPorts || pin < 0 || pin >= kPinsPerPort)
        return true; // invalid pins read as pulled up
    PinCell& c = pins[port][pin];
    if(c.read)
        return c.read();
    return c.level.load();
}

void Device::WritePin(int port, int pin, bool v)
{
    if(port < 0 || port >= kPorts || pin < 0 || pin >= kPinsPerPort)
        return;
    PinCell& c = pins[port][pin];
    c.level    = v;
    if(c.write)
        c.write(v);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
uint32_t Device::NowMs() const
{
    if(Lockstep())
        return uint32_t(samples.load() / (kSampleRate / 1000));
    auto d = std::chrono::steady_clock::now() - wall_epoch;
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}

uint32_t Device::NowUs() const
{
    if(Lockstep())
        return uint32_t(samples.load() * 1000 / (kSampleRate / 1000));
    auto d = std::chrono::steady_clock::now() - wall_epoch;
    return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(d).count());
}

void Device::FwWaitTurn()
{
    std::unique_lock<std::mutex> l(ls_m);
    ls_cv.wait(l, [&] { return fw_turn || !fw_should_run; });
}

void Device::FwYield()
{
    fw_getnow_budget = 0;
    std::unique_lock<std::mutex> l(ls_m);
    fw_turn = false;
    ls_cv.notify_all();
    ls_cv.wait(l, [&] { return fw_turn || !fw_should_run; });
}

void Device::HandBaton()
{
    std::unique_lock<std::mutex> l(ls_m);
    if(!fw_alive)
        return;
    fw_turn = true;
    ls_cv.notify_all();
    ls_cv.wait(l, [&] { return !fw_turn || !fw_alive; });
}

void Device::OnGetNow()
{
    if(!Lockstep() || !t_in_firmware_thread || t_irq_depth > 0 || !fw_should_run)
        return;
    if(++fw_getnow_budget >= 4096)
    {
        fw_getnow_budget = 0;
        FwYield();
    }
}

void Device::IsrDelayUs(uint64_t us)
{
    // A delay called from "interrupt" context (e.g. the SD card timer's 3 s
    // no-card animation loop). On the MCU the audio and DMA interrupts keep
    // running above it; here the audio thread is stuck in the callback, so let
    // the sample clock advance anyway and keep the one-shot events (LED chain
    // restarts, MIDI transmit completions) running one millisecond at a time.
    constexpr uint64_t step_us = 1000;
    while(us > 0)
    {
        uint64_t slice = std::min(us, step_us);
        if(!Lockstep())
            std::this_thread::sleep_for(std::chrono::microseconds(slice));
        samples += slice * kSampleRate / 1000000;
        RunOneShotsDue(samples.load());
        us -= slice;
    }
}

void Device::Delay(uint32_t ms)
{
    if(!t_in_firmware_thread)
    {
        IsrDelayUs(uint64_t(ms) * 1000);
        return;
    }
    if(!Lockstep())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return;
    }
    const uint32_t target = NowMs() + ms;
    while(fw_should_run && int32_t(target - NowMs()) > 0)
        FwYield();
}

void Device::DelayUs(uint32_t us)
{
    if(!t_in_firmware_thread)
    {
        IsrDelayUs(us);
        return;
    }
    if(!Lockstep())
    {
        std::this_thread::sleep_for(std::chrono::microseconds(us));
        return;
    }
    constexpr uint32_t block_us = 1000000u * kBlockSize / kSampleRate; // 500
    fw_delay_debt_us += us;
    while(fw_should_run && fw_delay_debt_us >= block_us)
    {
        fw_delay_debt_us -= block_us;
        FwYield();
    }
}

// ---------------------------------------------------------------------------
// Scheduler
// ---------------------------------------------------------------------------
void Device::ScheduleIn(double seconds, Fn fn)
{
    std::lock_guard<std::mutex> l(ev_m);
    uint64_t due = samples.load() + uint64_t(seconds * kSampleRate + 0.5);
    events.push(ScheduledEvent{due, event_seq++, std::move(fn)});
}

TimerModel* Device::GetTimer(int periph)
{
    for(auto* t : timers)
        if(t->periph == periph)
            return t;
    auto* t   = new TimerModel();
    t->periph = periph;
    timers.push_back(t);
    return t;
}

void Device::RunDue(uint64_t now)
{
    // periodic timers
    for(auto* t : timers)
    {
        if(!t->started || !t->enable_irq || !t->cb)
            continue;
        double per = t->PeriodSamples();
        if(per < 1.0)
            per = 1.0;
        int guard = 0;
        while(t->next_due <= double(now) && guard++ < int(blocksize) + 1)
        {
            t->cb(t->ctx);
            t->next_due += per;
        }
        if(t->next_due < double(now))
            t->next_due = double(now) + per;
    }
    RunOneShotsDue(now);
}

void Device::RunOneShotsDue(uint64_t now)
{
    // one-shot events (callbacks may schedule new events)
    for(;;)
    {
        Fn fn;
        {
            std::lock_guard<std::mutex> l(ev_m);
            if(events.empty() || events.top().due > now)
                break;
            fn = events.top().fn;
            events.pop();
        }
        if(fn)
            fn();
    }
}

void Device::TickModels(uint32_t now_ms)
{
    for(auto& e : enc)
        e.Tick(now_ms);
}

// ---------------------------------------------------------------------------
// MIDI
// ---------------------------------------------------------------------------
void Device::DeliverMidiIn()
{
    std::vector<uint8_t> uart_bytes, usb_bytes;
    {
        std::lock_guard<std::mutex> l(midi_m);
        uart_bytes.swap(midi_in_pending);
        usb_bytes.swap(usb_in_pending);
    }
    if(!uart_bytes.empty())
    {
        if(uart_listening && uart_rx_cb)
            uart_rx_cb(uart_bytes.data(), uart_bytes.size(), uart_rx_ctx, daisy::UartHandler::Result::OK);
    }
    if(!usb_bytes.empty())
    {
        if(usb_listening && usb_rx_cb)
            usb_rx_cb(usb_bytes.data(), usb_bytes.size(), usb_rx_ctx);
    }
}

// ---------------------------------------------------------------------------
// MP2722 battery charger (I2C address 0x3F)
// ---------------------------------------------------------------------------
void Device::MpTransmit(const uint8_t* data, size_t n)
{
    std::lock_guard<std::mutex> l(mp_m);
    if(n == 0)
        return;
    mp_ptr = data[0];
    for(size_t i = 1; i < n; i++)
    {
        uint8_t reg  = uint8_t(mp_ptr + (i - 1));
        mp_regs[reg] = data[i];
        if(reg == 0x08 && data[i] == 0xBF)
            Log("[sim] MP2722: shipping mode requested (power off)", true);
    }
}

void Device::MpReceive(uint8_t* data, size_t n)
{
    std::lock_guard<std::mutex> l(mp_m);
    const bool plugged = usb_power.load();
    const bool low     = batt_low.load();
    // status registers read by CHOMPI, starting at 0x11
    mp_regs[0x11] = 0x00;                                   // IINDPM_STAT = 0
    mp_regs[0x12] = uint8_t((plugged ? 0x40 : 0) | (plugged ? 0x20 : 0)); // VIN_GD, VIN_RDY
    mp_regs[0x13] = uint8_t(plugged ? (0x05 << 5) : 0x00); // CHG_STAT: 101 = charge done
    mp_regs[0x14] = 0x00;                                   // no NTC / battery faults
    mp_regs[0x15] = 0x00;
    mp_regs[0x16] = uint8_t(low ? 0x10 : 0x00);            // BATT_LOW_STAT
    for(size_t i = 0; i < n; i++)
        data[i] = mp_regs[uint8_t(mp_ptr + i)];
}

// ---------------------------------------------------------------------------
// LEDs: decode the PWM pulse-length DMA buffer that the firmware builds for
// its WS2812-style chains. 24 bits per LED, pulse >= 15 ticks means 1.
// ---------------------------------------------------------------------------
void Device::LedDmaStart(bool smt_chain, const uint32_t* data, size_t n)
{
    constexpr int    porch = 6;
    const size_t     nleds = n / 24;
    std::vector<Rgb> decoded;
    for(size_t i = porch; i + porch < nleds; i++)
    {
        uint8_t bytes[3] = {0, 0, 0};
        for(int k = 0; k < 3; k++)
            for(int j = 0; j < 8; j++)
                if(data[i * 24 + k * 8 + j] >= 15)
                    bytes[k] |= uint8_t(1 << (7 - j));
        Rgb c;
        if(smt_chain) // firmware writes G, R, B for the keybed chain, scaled by 1/4
        {
            c.r = uint8_t(std::min(255, bytes[1] * 4));
            c.g = uint8_t(std::min(255, bytes[0] * 4));
            c.b = uint8_t(std::min(255, bytes[2] * 4));
        }
        else // R, G, B for the panel chain, scaled by 1/11
        {
            c.r = uint8_t(std::min(255, bytes[0] * 11));
            c.g = uint8_t(std::min(255, bytes[1] * 11));
            c.b = uint8_t(std::min(255, bytes[2] * 11));
        }
        decoded.push_back(c);
    }
    {
        std::lock_guard<std::mutex> l(led_m);
        Rgb* dst = smt_chain ? key_leds : panel_leds;
        int  cnt = smt_chain ? kNumKeyLeds : kNumPanelLeds;
        for(int i = 0; i < cnt && i < int(decoded.size()); i++)
            dst[i] = decoded[i];
    }
    if(smt_chain)
        stats.led_frames++;
}

void Device::LedDmaStart(bool smt_chain, const uint32_t* data, size_t n, Fn done)
{
    LedDmaStart(smt_chain, data, n);
    // 1.2 us per bit on the wire, plus the reset gap
    double seconds = double(n) * 1.2e-6 + 50e-6;
    if(done)
        ScheduleIn(seconds, std::move(done));
}

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------
void Device::Log(const char* text, bool newline)
{
    std::lock_guard<std::mutex> l(log_m);
    log_partial += text;
    if(newline)
    {
        if(cfg.verbose)
            fprintf(stderr, "%s\n", log_partial.c_str());
        log.push_back(log_partial);
        if(log.size() > 2000)
            log.erase(log.begin(), log.begin() + 1000);
        log_partial.clear();
    }
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
void Device::RenderBlock(const float* const* in, float* const* out)
{
    auto t0 = std::chrono::steady_clock::now();
    static thread_local float zeros[kNumInputs][kBlockSize] = {};
    const float* inp[kNumInputs];
    for(int c = 0; c < kNumInputs; c++)
        inp[c] = in ? in[c] : zeros[c];

    {
        std::unique_lock<std::recursive_mutex> irq(irq_mutex, std::defer_lock);
        if(!Lockstep())
            irq.lock();
        const uint64_t now = samples.load();
        TickModels(NowMs());
        DeliverMidiIn();
        RunDue(now);
        if(audio_cb)
            audio_cb(inp, const_cast<float**>(out), blocksize);
        else
            for(int c = 0; c < num_channels; c++)
                std::fill(out[c], out[c] + blocksize, 0.f);
        samples += blocksize;
        stats.blocks_rendered++;
    }
    if(Lockstep())
        HandBaton();
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    if(us > stats.max_block_us)
        stats.max_block_us = us;
}

void Device::RenderStereo(float* interleaved, size_t frames, int pair)
{
    std::lock_guard<std::mutex> l(render_m);
    float  buf[kNumOutputs][kBlockSize];
    float* outp[kNumOutputs] = {buf[0], buf[1], buf[2], buf[3]};
    if(carry.size() != 2 * kBlockSize)
    {
        carry.assign(2 * kBlockSize, 0.f);
        carry_pos = 2 * kBlockSize;
    }
    size_t done = 0;
    while(done < frames)
    {
        if(carry_pos >= 2 * kBlockSize)
        {
            RenderBlock(nullptr, outp);
            int l = pair == 0 ? 0 : 2, r = pair == 0 ? 1 : 3;
            for(int i = 0; i < kBlockSize; i++)
            {
                carry[2 * i]     = buf[l][i];
                carry[2 * i + 1] = buf[r][i];
            }
            carry_pos = 0;
        }
        size_t avail = (2 * kBlockSize - carry_pos) / 2;
        size_t take  = std::min(avail, frames - done);
        std::memcpy(interleaved + 2 * done, carry.data() + carry_pos, take * 2 * sizeof(float));
        carry_pos += take * 2;
        done += take;
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
/** The Daisy Seed's 64 MB SDRAM lives at 0xC0000000. The simulator provides a
 *  zero-filled block of that size and hands it to patched firmware through
 *  chompi_sim_sdram(). As a courtesy to code that might still hard-code the
 *  address, it first tries to place the block at 0xC0000000; that works on
 *  Linux but not on macOS, where the low 4 GB are reserved and the executable
 *  cannot change that for arm64. Either way the block is usable. */
bool Device::MapSdram()
{
    const size_t len = size_t(64) << 20;
    void*        want = reinterpret_cast<void*>(uintptr_t(0xC0000000u));
    int          flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_FIXED_NOREPLACE
    flags |= MAP_FIXED_NOREPLACE;
#endif
    void* got = MAP_FAILED;
    if(!getenv("CHOMPI_SIM_NO_FIXED_SDRAM")) // set it to exercise the macOS path on Linux
        got = mmap(want, len, PROT_READ | PROT_WRITE, flags, -1, 0);
    if(got != MAP_FAILED && got != want)
    {
        munmap(got, len);
        got = MAP_FAILED;
    }
    if(got == MAP_FAILED)
    {
        // anywhere will do: calloc gives lazily committed zero pages
        got = calloc(len, 1);
        if(!got)
            return false;
        sdram_at_hw_address = false;
    }
    else
        sdram_at_hw_address = true;
    sdram = got;
    return true;
}

bool Device::Init(const Config& c)
{
    cfg = c;
    if(!std::filesystem::is_directory(cfg.card_dir))
    {
        fprintf(stderr, "[sim] card folder not found: %s\n", cfg.card_dir.c_str());
        return false;
    }
    SetCardRoot(cfg.card_dir);
    wall_epoch = std::chrono::steady_clock::now();
    if(!sdram && !MapSdram())
    {
        fprintf(stderr, "[sim] could not allocate the 64 MB SDRAM stand-in\n");
        return false;
    }
    if(cfg.verbose && !sdram_at_hw_address)
        fprintf(stderr, "[sim] SDRAM stand-in is not at 0xC0000000 (normal on macOS); patched firmware uses chompi_sim_sdram()\n");

    // ---- button chain: 5 x CD4021 on D8 (clk) / D7 (latch) / D9 (data) ----
    button_chain.nbits = NUM_BUTTONS;
    button_chain.bit   = [this](int i) -> bool {
        switch(i)
        {
            case SW_TOG: return !toggle_down.load();
            case ENC_1_SW: return !enc[0].pressed.load();
            case ENC_2_SW: return !enc[1].pressed.load();
            case ENC_3_SW: return !enc[2].pressed.load();
            case ENC_4_SW: return !enc[3].pressed.load();
            case ENC_6_SW: return !enc[5].pressed.load();
            default: return !button_pressed[i].load();
        }
    };
    auto hookChain = [this](Chain4021& ch, daisy::Pin clk, daisy::Pin latch, daisy::Pin data) {
        pins[clk.port][clk.pin].write     = [&ch](bool v) { ch.OnClk(v); };
        pins[latch.port][latch.pin].write = [&ch](bool v) { ch.OnLatch(v); };
        pins[data.port][data.pin].read    = [&ch]() { return ch.Data(); };
    };
    hookChain(button_chain, daisy::seed::D8, daisy::seed::D7, daisy::seed::D9);

    // ---- encoder chain: one CD4021 with A/B of SW1..SW4 on D22 / D23 / D19 ----
    encoder_chain.nbits = 8;
    encoder_chain.bit   = [this](int i) -> bool { return (i & 1) ? enc[i / 2].b.load() : enc[i / 2].a.load(); };
    hookChain(encoder_chain, daisy::seed::D22, daisy::seed::D23, daisy::seed::D19);

    // ---- SW5 (big knob) on GPIO: A = D0, B = D20, switch = D10; SW6: A = D15, B = D17 ----
    using daisy::seed::D0;
    pins[D0.port][D0.pin].read                                 = [this] { return enc[4].a.load(); };
    pins[daisy::seed::D20.port][daisy::seed::D20.pin].read     = [this] { return enc[4].b.load(); };
    pins[daisy::seed::D10.port][daisy::seed::D10.pin].read     = [this] { return !enc[4].pressed.load(); };
    pins[daisy::seed::D15.port][daisy::seed::D15.pin].read     = [this] { return enc[5].a.load(); };
    pins[daisy::seed::D17.port][daisy::seed::D17.pin].read     = [this] { return enc[5].b.load(); };
    // jack detect (D21) reads low = nothing plugged; charger interrupt (D31) idles high
    pins[daisy::seed::D21.port][daisy::seed::D21.pin].level    = false;
    pins[daisy::seed::D31.port][daisy::seed::D31.pin].level    = true;

    initialised = true;
    return true;
}

void Device::Start()
{
    if(!initialised || fw_alive)
        return;
    fw_should_run = true;
    fw_alive      = true;
    fw_thread     = std::thread([this] {
        t_in_firmware_thread = true;
        if(Lockstep())
            FwWaitTurn();
        if(fw_should_run)
            chompi_firmware_main();
        {
            std::lock_guard<std::mutex> l(ls_m);
            fw_alive = false;
            fw_turn  = false;
        }
        ls_cv.notify_all();
    });
}

void Device::Stop()
{
    if(null_run)
    {
        null_run = false;
        if(null_thread.joinable())
            null_thread.join();
    }
    fw_should_run = false;
    {
        std::lock_guard<std::mutex> l(ls_m);
        fw_turn = true;
    }
    ls_cv.notify_all();
    if(fw_thread.joinable())
    {
        // the firmware loop may be inside a long blocking wait; don't hang forever
        std::unique_lock<std::mutex> l(ls_m);
        bool ended = ls_cv.wait_for(l, std::chrono::seconds(2), [&] { return !fw_alive.load(); });
        l.unlock();
        if(ended)
            fw_thread.join();
        else
        {
            fprintf(stderr, "[sim] firmware thread did not exit, detaching\n");
            fw_thread.detach();
        }
    }
}

} // namespace dev

// ---------------------------------------------------------------------------
// Public facade
// ---------------------------------------------------------------------------
using dev::Device;

Sim& Sim::Get()
{
    static Sim s;
    return s;
}
bool Sim::Init(const Config& cfg) { return Device::Get().Init(cfg); }
void Sim::Start() { Device::Get().Start(); }
void Sim::Stop() { Device::Get().Stop(); }
bool Sim::FirmwareRunning() const { return Device::Get().fw_alive; }
const Config& Sim::GetConfig() const { return Device::Get().cfg; }

void Sim::RenderBlock(const float* const* in, float* const* out) { Device::Get().RenderBlock(in, out); }
void Sim::RenderStereo(float* interleaved, size_t frames, int pair) { Device::Get().RenderStereo(interleaved, frames, pair); }

void Sim::StartNullAudio()
{
    Device& d = Device::Get();
    if(d.null_run)
        return;
    d.null_run    = true;
    d.null_thread = std::thread([&d] {
        float  buf[kNumOutputs][kBlockSize];
        float* outp[kNumOutputs] = {buf[0], buf[1], buf[2], buf[3]};
        auto   next = std::chrono::steady_clock::now();
        while(d.null_run)
        {
            d.RenderBlock(nullptr, outp);
            next += std::chrono::microseconds(1000000 * kBlockSize / kSampleRate);
            std::this_thread::sleep_until(next);
        }
    });
}
void Sim::StopNullAudio()
{
    Device& d = Device::Get();
    if(!d.null_run)
        return;
    d.null_run = false;
    if(d.null_thread.joinable())
        d.null_thread.join();
}

void Sim::SetButton(int button, bool pressed)
{
    if(button < 0 || button >= NUM_BUTTONS)
        return;
    Device::Get().button_pressed[button] = pressed;
}
void Sim::SetToggle(bool down) { Device::Get().toggle_down = down; }
void Sim::TurnEncoder(int encoder, int detents)
{
    if(encoder < 0 || encoder >= kNumEncoders)
        return;
    Device::Get().enc[encoder].pending += detents;
}
void Sim::SetEncoderPressed(int encoder, bool pressed)
{
    if(encoder < 0 || encoder >= kNumEncoders)
        return;
    Device::Get().enc[encoder].pressed = pressed;
}
bool Sim::ButtonPressed(int button) const
{
    if(button < 0 || button >= NUM_BUTTONS)
        return false;
    return Device::Get().button_pressed[button];
}
bool Sim::ToggleDown() const { return Device::Get().toggle_down; }

void Sim::MidiIn(const uint8_t* bytes, size_t n, bool usb)
{
    Device& d = Device::Get();
    std::lock_guard<std::mutex> l(d.midi_m);
    auto& v = usb ? d.usb_in_pending : d.midi_in_pending;
    v.insert(v.end(), bytes, bytes + n);
}
std::vector<uint8_t> Sim::TakeMidiOut()
{
    Device& d = Device::Get();
    std::lock_guard<std::mutex> l(d.midi_m);
    std::vector<uint8_t> v;
    v.swap(d.midi_out);
    return v;
}
std::vector<uint8_t> Sim::TakeUsbMidiOut()
{
    Device& d = Device::Get();
    std::lock_guard<std::mutex> l(d.midi_m);
    std::vector<uint8_t> v;
    v.swap(d.usb_out);
    return v;
}

Rgb Sim::KeyLed(int chainIndex) const
{
    Device& d = Device::Get();
    if(chainIndex < 0 || chainIndex >= kNumKeyLeds)
        return Rgb{};
    std::lock_guard<std::mutex> l(d.led_m);
    return d.key_leds[chainIndex];
}
Rgb Sim::PanelLed(int chainIndex) const
{
    Device& d = Device::Get();
    if(chainIndex < 0 || chainIndex >= kNumPanelLeds)
        return Rgb{};
    std::lock_guard<std::mutex> l(d.led_m);
    return d.panel_leds[chainIndex];
}

void     Sim::SetUsbPower(bool plugged) { Device::Get().usb_power = plugged; }
void     Sim::SetBatteryLow(bool low) { Device::Get().batt_low = low; }
void     Sim::SetCardPresent(bool present) { dev::SetCardPresent(present); }
uint64_t Sim::SampleClock() const { return Device::Get().samples; }
uint32_t Sim::NowMs() const { return Device::Get().NowMs(); }
Stats    Sim::GetStats() const { return Device::Get().stats; }
std::vector<std::string> Sim::TakeLog()
{
    Device& d = Device::Get();
    std::lock_guard<std::mutex> l(d.log_m);
    std::vector<std::string> v;
    v.swap(d.log);
    return v;
}

const char* ButtonName(int button)
{
    static const char* names[NUM_BUTTONS] = {
        "ENC_1_SW", "ENC_2_SW", "ENC_3_SW", "ENC_4_SW", "NC_6", "KEY_26 (CHOMPI)", "SW_TOG", "KEY_16",
        "KEY_2", "KEY_3", "KEY_4", "KEY_5", "KEY_17", "KEY_18", "KEY_19", "KEY_1",
        "KEY_6", "KEY_7", "KEY_8", "KEY_9", "KEY_10", "KEY_20", "KEY_21", "KEY_22",
        "KEY_11", "KEY_12", "KEY_13", "KEY_14", "KEY_15", "KEY_23", "KEY_24", "KEY_25",
        "ENC_6_SW", "KEY_27 (PLAY)", "KEY_28 (LOOP)", "NC_1", "NC_2", "NC_3", "NC_4", "NC_5"};
    if(button < 0 || button >= NUM_BUTTONS)
        return "?";
    return names[button];
}

} // namespace chompi_sim

// ---- hooks used by the patched firmware ----
bool  chompi_sim_running() { return chompi_sim::dev::Device::Get().fw_should_run; }
void* chompi_sim_sdram()
{
    chompi_sim::dev::Device& d = chompi_sim::dev::Device::Get();
    if(!d.sdram)
        d.MapSdram();
    return d.sdram;
}
