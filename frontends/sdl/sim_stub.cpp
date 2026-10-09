/** Stand-in implementation of the simulator core used to develop and test the
 *  SDL front-end without the firmware. Built when -DCHOMPI_SIM_STUB=ON.
 *  Behaviour: pressed piano keys play a sine wave on all outputs and light their
 *  key LED white; panel LEDs slowly cycle colours; encoders change a gain. */
#include "chompi_sim/sim.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <chrono>

namespace chompi_sim
{
namespace
{
struct State
{
    Config   cfg;
    std::mutex m;
    bool     running = false;
    bool     buttons[NUM_BUTTONS] = {};
    bool     toggle = false;
    int      enc_pos[kNumEncoders] = {};
    bool     enc_pressed[kNumEncoders] = {};
    float    phase[25] = {};
    uint64_t samples = 0;
    std::vector<uint8_t> midi_in, midi_out, usb_out;
    std::vector<std::string> log;
    Stats    stats;
    std::thread null_audio;
    std::atomic<bool> null_audio_run{false};
    float    carry[2 * kBlockSize];
    size_t   carry_n = 0;
    bool     usb_power = true, batt_low = false;
};
State& S()
{
    static State s;
    return s;
}
} // namespace

Sim& Sim::Get()
{
    static Sim s;
    return s;
}
bool Sim::Init(const Config& cfg)
{
    S().cfg = cfg;
    S().log.push_back("stub core initialised");
    return true;
}
void Sim::Start() { S().running = true; }
void Sim::Stop()
{
    StopNullAudio();
    S().running = false;
}
bool          Sim::FirmwareRunning() const { return S().running; }
const Config& Sim::GetConfig() const { return S().cfg; }

void Sim::RenderBlock(const float* const* in, float* const* out)
{
    (void)in;
    State& s = S();
    std::lock_guard<std::mutex> l(s.m);
    float gain = 0.2f * std::pow(2.f, s.enc_pos[ENC_SW6] / 12.f);
    for(int i = 0; i < kBlockSize; i++)
    {
        float v = 0.f;
        for(int k = 0; k < 25; k++)
        {
            if(!s.buttons[kPianoKeys[k]])
                continue;
            float f = 261.63f * std::pow(2.f, (k + s.enc_pos[ENC_SW4]) / 12.f);
            s.phase[k] += f / kSampleRate;
            if(s.phase[k] >= 1.f)
                s.phase[k] -= 1.f;
            v += std::sin(6.2831853f * s.phase[k]) * gain;
        }
        for(int c = 0; c < kNumOutputs; c++)
            out[c][i] = v;
    }
    s.samples += kBlockSize;
    s.stats.blocks_rendered++;
}

void Sim::RenderStereo(float* interleaved, size_t frames, int pair)
{
    State& s = S();
    float  outbuf[kNumOutputs][kBlockSize];
    float* outp[kNumOutputs] = {outbuf[0], outbuf[1], outbuf[2], outbuf[3]};
    size_t done = 0;
    while(done < frames)
    {
        if(s.carry_n == 0)
        {
            RenderBlock(nullptr, outp);
            for(int i = 0; i < kBlockSize; i++)
            {
                s.carry[2 * i]     = outbuf[pair * 2][i];
                s.carry[2 * i + 1] = outbuf[pair * 2 + 1][i];
            }
            s.carry_n = kBlockSize;
        }
        size_t take = std::min(s.carry_n, frames - done);
        size_t off  = kBlockSize - s.carry_n;
        std::memcpy(interleaved + 2 * done, s.carry + 2 * off, take * 2 * sizeof(float));
        s.carry_n -= take;
        done += take;
    }
}

void Sim::StartNullAudio()
{
    State& s = S();
    if(s.null_audio_run)
        return;
    s.null_audio_run = true;
    s.null_audio     = std::thread([&s] {
        float  buf[kNumOutputs][kBlockSize];
        float* outp[kNumOutputs] = {buf[0], buf[1], buf[2], buf[3]};
        auto   next = std::chrono::steady_clock::now();
        while(s.null_audio_run)
        {
            Sim::Get().RenderBlock(nullptr, outp);
            next += std::chrono::microseconds(500);
            std::this_thread::sleep_until(next);
        }
    });
}
void Sim::StopNullAudio()
{
    State& s = S();
    if(!s.null_audio_run)
        return;
    s.null_audio_run = false;
    if(s.null_audio.joinable())
        s.null_audio.join();
}

void Sim::SetButton(int button, bool pressed)
{
    if(button < 0 || button >= NUM_BUTTONS)
        return;
    std::lock_guard<std::mutex> l(S().m);
    S().buttons[button] = pressed;
}
void Sim::SetToggle(bool down)
{
    std::lock_guard<std::mutex> l(S().m);
    S().toggle = down;
}
void Sim::TurnEncoder(int encoder, int detents)
{
    if(encoder < 0 || encoder >= kNumEncoders)
        return;
    std::lock_guard<std::mutex> l(S().m);
    S().enc_pos[encoder] += detents;
}
void Sim::SetEncoderPressed(int encoder, bool pressed)
{
    if(encoder < 0 || encoder >= kNumEncoders)
        return;
    std::lock_guard<std::mutex> l(S().m);
    S().enc_pressed[encoder] = pressed;
}
bool Sim::ButtonPressed(int button) const
{
    if(button < 0 || button >= NUM_BUTTONS)
        return false;
    return S().buttons[button];
}
bool Sim::ToggleDown() const { return S().toggle; }

void Sim::MidiIn(const uint8_t* bytes, size_t n, bool usb)
{
    (void)usb;
    std::lock_guard<std::mutex> l(S().m);
    S().midi_in.insert(S().midi_in.end(), bytes, bytes + n);
}
std::vector<uint8_t> Sim::TakeMidiOut()
{
    std::lock_guard<std::mutex> l(S().m);
    auto v = std::move(S().midi_out);
    S().midi_out.clear();
    return v;
}
std::vector<uint8_t> Sim::TakeUsbMidiOut()
{
    std::lock_guard<std::mutex> l(S().m);
    auto v = std::move(S().usb_out);
    S().usb_out.clear();
    return v;
}

Rgb Sim::KeyLed(int chainIndex) const
{
    for(int k = 0; k < 25; k++)
        if(kPianoKeyLed[k] == chainIndex && S().buttons[kPianoKeys[k]])
            return Rgb{255, 255, 255};
    // faint idle glow so the layout is visible
    return Rgb{uint8_t(10 + chainIndex * 2), 10, uint8_t(30)};
}
Rgb Sim::PanelLed(int chainIndex) const
{
    double t = S().samples / double(kSampleRate) + chainIndex * 0.6;
    auto   f = [](double x) { return uint8_t(127.5 + 127.5 * std::sin(x)); };
    return Rgb{f(t), f(t + 2.1), f(t + 4.2)};
}
void     Sim::SetUsbPower(bool plugged) { S().usb_power = plugged; }
void     Sim::SetBatteryLow(bool low) { S().batt_low = low; }
void     Sim::SetCardPresent(bool present) { (void)present; }
uint64_t Sim::SampleClock() const { return S().samples; }
uint32_t Sim::NowMs() const { return uint32_t(S().samples / (kSampleRate / 1000)); }
Stats    Sim::GetStats() const { return S().stats; }
// ---- audio inputs: the stand-in keeps the state and plays nothing ----
namespace
{
struct StubInput
{
    std::string name;
    bool        playing = false, loop = false, line_in = false;
    double      length_s = 0;
    uint64_t    started  = 0;
};
StubInput& I()
{
    static StubInput i;
    return i;
}
} // namespace
bool Sim::LoadInputFile(const std::string& path, std::string& err)
{
    (void)err;
    I().name     = path.substr(path.find_last_of("/\\") + 1);
    I().length_s = 1.0;
    I().playing  = false;
    return true;
}
void Sim::PlayInput(bool loop, float gain)
{
    (void)gain;
    I().loop    = loop;
    I().playing = !I().name.empty();
    I().started = S().samples;
}
void Sim::StopInput() { I().playing = false; }
InputState Sim::GetInputState() const
{
    InputState st;
    st.name     = I().name;
    st.loop     = I().loop;
    st.length_s = I().length_s;
    st.line_in  = I().line_in;
    double pos  = double(S().samples - I().started) / kSampleRate;
    if(I().playing && pos >= st.length_s && !I().loop)
        I().playing = false;
    st.playing    = I().playing;
    st.position_s = st.playing ? std::fmod(pos, st.length_s) : 0.0;
    return st;
}
void Sim::SetLineIn(bool plugged) { I().line_in = plugged; }
bool Sim::LineIn() const { return I().line_in; }
void Sim::PushInput(const float* mono, size_t frames)
{
    (void)mono;
    (void)frames;
}
float Sim::TakeInputPeak() { return 0.f; }

std::vector<std::string> Sim::TakeLog()
{
    std::lock_guard<std::mutex> l(S().m);
    auto v = std::move(S().log);
    S().log.clear();
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
