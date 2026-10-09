/** @file main.cpp
 *  @brief Headless CHOMPI simulator: runs the firmware in deterministic
 *  lockstep, plays a script of key presses / knob turns / MIDI, and writes the
 *  audio to a WAV file plus LED snapshots and a picture of the panel.
 *
 *  Script lines ("#" starts a comment), times in seconds:
 *    2.0 note 12 0.5          press piano semitone 12 (0 = low C) for 0.5 s
 *    2.0 key 12 down|up       raw piano key state
 *    3.0 press PLAY 0.1       press a button by name (PLAY LOOP CHOMPI KEY_27 ENC_1_SW ...) or id
 *    3.0 button PLAY down|up
 *    4.0 enc 4 -3             turn physical encoder 0..5 by detents (+ = clockwise)
 *    4.5 encpress 5 0.1       push an encoder for a duration
 *    5.0 toggle down|up       mode switch
 *    6.0 midi 90 3C 7F        bytes to the TRS MIDI input (hex)
 *    7.0 usbmidi 90 3C 7F     bytes to the USB MIDI input
 *    8.0 leds                 append an LED snapshot to the --leds file
 *    9.0 card out|in          pull the SD card / put it back
 *    9.0 power off|on         unplug USB power / plug it in
 *    9.0 battery low|ok       battery state reported by the charger
 */
#include "chompi_sim/sim.h"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <chrono>
#include <thread>
#include <sstream>
#include <string>
#include <vector>

using namespace chompi_sim;

namespace
{
struct Event
{
    double                t;
    std::function<void()> fn;
};

int ParseButton(const std::string& s)
{
    std::string u = s;
    for(auto& c : u)
        c = char(toupper(uint8_t(c)));
    if(u == "PLAY")
        return KEY_PLAY;
    if(u == "LOOP")
        return KEY_LOOP;
    if(u == "CHOMPI")
        return KEY_CHOMPI;
    for(int i = 0; i < NUM_BUTTONS; i++)
    {
        std::string n = ButtonName(i);
        n             = n.substr(0, n.find(' '));
        if(n == u)
            return i;
    }
    if(!u.empty() && isdigit(uint8_t(u[0])))
        return atoi(u.c_str());
    return -1;
}

std::vector<uint8_t> ParseHex(std::istringstream& ss)
{
    std::vector<uint8_t> v;
    std::string          tok;
    while(ss >> tok)
        v.push_back(uint8_t(strtoul(tok.c_str(), nullptr, 16)));
    return v;
}

void WriteWav(const std::string& path, const std::vector<int16_t>& stereo, int rate)
{
    std::ofstream f(path, std::ios::binary);
    auto          u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto          u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    uint32_t      data_bytes = uint32_t(stereo.size() * 2);
    f.write("RIFF", 4);
    u32(36 + data_bytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    u32(16);
    u16(1);
    u16(2);
    u32(uint32_t(rate));
    u32(uint32_t(rate * 4));
    u16(4);
    u16(16);
    f.write("data", 4);
    u32(data_bytes);
    f.write(reinterpret_cast<const char*>(stereo.data()), data_bytes);
}

std::string LedSnapshot(double t)
{
    std::ostringstream o;
    o << "t=" << t << " keys:";
    for(int k = 0; k < 25; k++)
    {
        Rgb c = Sim::Get().KeyLed(kPianoKeyLed[k]);
        o << " " << int(c.r) << "," << int(c.g) << "," << int(c.b);
    }
    o << " panel:";
    for(int i = 0; i < kNumPanelLeds; i++)
    {
        Rgb c = Sim::Get().PanelLed(i);
        o << " " << int(c.r) << "," << int(c.g) << "," << int(c.b);
    }
    return o.str();
}

/** Minimal software renderer producing a binary PPM of the panel. */
struct Image
{
    int                  w, h;
    std::vector<uint8_t> px;
    Image(int W, int H) : w(W), h(H), px(size_t(W) * H * 3, 0) {}
    void Set(int x, int y, Rgb c)
    {
        if(x < 0 || y < 0 || x >= w || y >= h)
            return;
        uint8_t* p = &px[(size_t(y) * w + x) * 3];
        p[0]       = c.r;
        p[1]       = c.g;
        p[2]       = c.b;
    }
    void Rect(int x, int y, int rw, int rh, Rgb c)
    {
        for(int j = y; j < y + rh; j++)
            for(int i = x; i < x + rw; i++)
                Set(i, j, c);
    }
    void Circle(int cx, int cy, int r, Rgb c)
    {
        for(int j = -r; j <= r; j++)
            for(int i = -r; i <= r; i++)
                if(i * i + j * j <= r * r)
                    Set(cx + i, cy + j, c);
    }
    void Save(const std::string& path)
    {
        std::ofstream f(path, std::ios::binary);
        f << "P6\n" << w << " " << h << "\n255\n";
        f.write(reinterpret_cast<const char*>(px.data()), std::streamsize(px.size()));
    }
};

Rgb Glow(Rgb c, Rgb base)
{
    auto mix = [](uint8_t led, uint8_t b) { return uint8_t(std::min(255, int(b) + int(led))); };
    return Rgb{mix(c.r, base.r), mix(c.g, base.g), mix(c.b, base.b)};
}

void RenderPanel(const std::string& path)
{
    // Geometry from the Rev4 board file (mm, EAGLE y up), 3.4 px per mm.
    Sim&  sim = Sim::Get();
    Image im(1120, 360);
    auto  X   = [](float mm) { return int(16 + mm * 3.4f); };
    auto  Y   = [](float mm) { return int(14 + (99.68f - mm) * 3.4f); };
    im.Rect(0, 0, im.w, im.h, Rgb{232, 226, 212});
    static const float key_x[25]   = {22.91f, 32.95f, 43.02f, 53.07f, 63.13f, 83.24f, 93.29f, 103.35f, 113.40f,
                                      123.46f, 133.51f, 143.57f, 163.68f, 173.73f, 183.79f, 193.84f, 203.91f, 224.02f,
                                      234.07f, 244.13f, 254.18f, 264.24f, 274.29f, 284.35f, 304.46f};
    static const bool  key_upper[25] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0};
    const int          cap           = int(18.2f * 3.4f);
    for(int k = 0; k < 25; k++)
    {
        Rgb  led  = sim.KeyLed(kPianoKeyLed[k]);
        bool p    = sim.ButtonPressed(kPianoKeys[k]);
        Rgb  base = key_upper[k] ? Rgb{214, 211, 203} : Rgb{248, 247, 242};
        if(p)
            base = Rgb{uint8_t(base.r - 30), uint8_t(base.g - 30), uint8_t(base.b - 30)};
        float b = std::max({led.r, led.g, led.b}) / 255.f;
        if(b > 0)
        {
            auto mix = [b](uint8_t a, uint8_t l) { return uint8_t(a * (1 - 0.9f * b) + (l / b) * 0.9f * b); };
            base = Rgb{mix(base.r, led.r), mix(base.g, led.g), mix(base.b, led.b)};
        }
        im.Rect(X(key_x[k]) - cap / 2, Y(key_upper[k] ? 36.56f : 16.56f) - cap / 2, cap, cap, base);
    }
    struct F { int button; float x; } fk[3] = {{KEY_CHOMPI, 43.03f}, {KEY_PLAY, 244.14f}, {KEY_LOOP, 264.26f}};
    for(auto& f : fk)
        im.Rect(X(f.x) - cap / 2, Y(65.92f) - cap / 2, cap, cap, sim.ButtonPressed(f.button) ? Rgb{200, 192, 176} : Rgb{238, 232, 218});
    struct K { float x, y, r; bool big; } knobs[6] = {{69.39f, 68.46f, 8.5f, false}, {102.90f, 68.46f, 8.5f, false},
                                                     {136.42f, 68.46f, 8.5f, false}, {169.94f, 68.46f, 8.5f, false},
                                                     {210.09f, 68.85f, 15.5f, true}, {300.65f, 68.46f, 8.5f, false}};
    for(auto& k : knobs)
        im.Circle(X(k.x), Y(k.y), int(k.r * 3.4f), k.big ? Rgb{112, 80, 176} : Rgb{58, 59, 66});
    struct L { float x, y, r; } leds[kNumPanelLeds] = {{39.22f, 89.55f, 4}, {69.38f, 89.55f, 4}, {102.90f, 89.55f, 4},
                                                       {136.42f, 89.55f, 4}, {169.94f, 89.55f, 4}, {200.11f, 91.86f, 2.5f},
                                                       {220.22f, 91.86f, 2.5f}, {240.33f, 91.86f, 2.5f}, {260.44f, 91.86f, 2.5f},
                                                       {300.65f, 89.55f, 4}};
    for(int i = 0; i < kNumPanelLeds; i++)
        im.Circle(X(leds[i].x), Y(leds[i].y), int(leds[i].r * 3.4f), Glow(sim.PanelLed(i), Rgb{150, 144, 132}));
    im.Rect(X(18.38f) - 8, Y(68.46f) - 23, 16, 46, Rgb{58, 56, 52});
    im.Rect(X(18.38f) - 12, sim.ToggleDown() ? Y(68.46f) + 4 : Y(68.46f) - 22, 24, 18, Rgb{236, 234, 228});
    im.Save(path);
}

} // namespace

int main(int argc, char** argv)
{
    std::string card = "card", wav, script, leds_path, ppm_path, midi_path;
    double      seconds  = 10.0;
    int         pair     = 1;
    bool        quiet    = false;
    bool        realtime = false;
    bool        trace    = false;
    int         burst    = 1; /**< realtime: blocks rendered back to back, like a sound card buffer */
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto        next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if(a == "--card")
            card = next();
        else if(a == "--seconds")
            seconds = atof(next().c_str());
        else if(a == "--wav")
            wav = next();
        else if(a == "--pair")
            pair = atoi(next().c_str());
        else if(a == "--script")
            script = next();
        else if(a == "--leds")
            leds_path = next();
        else if(a == "--ppm")
            ppm_path = next();
        else if(a == "--midi-out")
            midi_path = next();
        else if(a == "--quiet")
            quiet = true;
        else if(a == "--realtime")
            realtime = true;
        else if(a == "--trace")
            trace = true;
        else if(a == "--burst")
            burst = std::max(1, atoi(next().c_str()));
        else if(a == "--cards" || a == "--firmware")
            next(); // launcher options, passed through; this executable is one firmware
        else
        {
            fprintf(stderr, "usage: chompi-sim --card DIR [--seconds N] [--wav out.wav] [--pair 0|1] [--script file] [--leds out.txt] [--ppm panel.ppm] [--midi-out out.bin] [--quiet] [--realtime [--burst N]] [--trace]\n");
            return 2;
        }
    }

    Config cfg;
    cfg.card_dir = card;
    cfg.realtime = realtime; // --realtime exercises the free-running thread mode at wall-clock pace
    cfg.verbose  = !quiet;
    Sim& sim     = Sim::Get();
    if(!sim.Init(cfg))
        return 1;

    // ---- parse the script ----
    std::vector<Event> events;
    std::ofstream      leds_file;
    if(!leds_path.empty())
        leds_file.open(leds_path);
    if(!script.empty())
    {
        std::ifstream f(script);
        if(!f)
        {
            fprintf(stderr, "cannot open script %s\n", script.c_str());
            return 1;
        }
        std::string line;
        int         lineno = 0;
        while(std::getline(f, line))
        {
            lineno++;
            auto hash = line.find('#');
            if(hash != std::string::npos)
                line = line.substr(0, hash);
            std::istringstream ss(line);
            double             t;
            std::string        cmd;
            if(!(ss >> t >> cmd))
                continue;
            if(cmd == "note" || cmd == "key")
            {
                int         semi;
                std::string arg;
                ss >> semi >> arg;
                if(semi < 0 || semi > 24)
                {
                    fprintf(stderr, "script line %d: semitone out of range\n", lineno);
                    return 1;
                }
                int b = kPianoKeys[semi];
                if(cmd == "note")
                {
                    double dur = atof(arg.c_str());
                    events.push_back({t, [b] { Sim::Get().SetButton(b, true); }});
                    events.push_back({t + dur, [b] { Sim::Get().SetButton(b, false); }});
                }
                else
                {
                    bool down = arg == "down";
                    events.push_back({t, [b, down] { Sim::Get().SetButton(b, down); }});
                }
            }
            else if(cmd == "press" || cmd == "button")
            {
                std::string name, arg;
                ss >> name >> arg;
                int b = ParseButton(name);
                if(b < 0)
                {
                    fprintf(stderr, "script line %d: unknown button %s\n", lineno, name.c_str());
                    return 1;
                }
                if(cmd == "press")
                {
                    double dur = atof(arg.c_str());
                    events.push_back({t, [b] { Sim::Get().SetButton(b, true); }});
                    events.push_back({t + dur, [b] { Sim::Get().SetButton(b, false); }});
                }
                else
                {
                    bool down = arg == "down";
                    events.push_back({t, [b, down] { Sim::Get().SetButton(b, down); }});
                }
            }
            else if(cmd == "enc")
            {
                int e, d;
                ss >> e >> d;
                events.push_back({t, [e, d] { Sim::Get().TurnEncoder(e, d); }});
            }
            else if(cmd == "encpress")
            {
                int    e;
                double dur;
                ss >> e >> dur;
                events.push_back({t, [e] { Sim::Get().SetEncoderPressed(e, true); }});
                events.push_back({t + dur, [e] { Sim::Get().SetEncoderPressed(e, false); }});
            }
            else if(cmd == "toggle")
            {
                std::string arg;
                ss >> arg;
                bool down = arg == "down";
                events.push_back({t, [down] { Sim::Get().SetToggle(down); }});
            }
            else if(cmd == "midi" || cmd == "usbmidi")
            {
                auto bytes = ParseHex(ss);
                bool usb   = cmd == "usbmidi";
                events.push_back({t, [bytes, usb] { Sim::Get().MidiIn(bytes.data(), bytes.size(), usb); }});
            }
            else if(cmd == "card" || cmd == "power" || cmd == "battery")
            {
                std::string arg;
                ss >> arg;
                bool on = (arg == "in" || arg == "on" || arg == "ok");
                if(cmd == "card")
                    events.push_back({t, [on] { Sim::Get().SetCardPresent(on); }});
                else if(cmd == "power")
                    events.push_back({t, [on] { Sim::Get().SetUsbPower(on); }});
                else
                    events.push_back({t, [on] { Sim::Get().SetBatteryLow(!on); }});
            }
            else if(cmd == "input")
            {
                // input FILE [loop] [gain]: play a WAV into the mic and aux inputs; "input stop" stops it
                std::string file;
                ss >> file;
                if(file == "stop")
                    events.push_back({t, [] { Sim::Get().StopInput(); }});
                else
                {
                    if(!std::filesystem::is_regular_file(file))
                    {
                        fprintf(stderr, "script line %d: input file %s not found\n", lineno, file.c_str());
                        return 1;
                    }
                    bool        loop = false;
                    float       gain = 1.f;
                    std::string tok;
                    while(ss >> tok)
                    {
                        if(tok == "loop")
                            loop = true;
                        else
                            gain = float(atof(tok.c_str()));
                    }
                    events.push_back({t, [file, loop, gain] {
                                          std::string err;
                                          if(Sim::Get().LoadInputFile(file, err))
                                              Sim::Get().PlayInput(loop, gain);
                                          else
                                              fprintf(stderr, "%s\n", err.c_str());
                                      }});
                }
            }
            else if(cmd == "jack")
            {
                std::string arg;
                ss >> arg;
                bool in = arg == "in" || arg == "plugged";
                events.push_back({t, [in] { Sim::Get().SetLineIn(in); }});
            }
            else if(cmd == "leds")
            {
                events.push_back({t, [&leds_file, t] {
                                      std::string s = LedSnapshot(t);
                                      if(leds_file.is_open())
                                          leds_file << s << "\n";
                                      else
                                          printf("%s\n", s.c_str());
                                  }});
            }
            else
            {
                fprintf(stderr, "script line %d: unknown command %s\n", lineno, cmd.c_str());
                return 1;
            }
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.t < b.t; });

    // ---- run ----
    sim.Start();
    const uint64_t       total_blocks = uint64_t(seconds * kSampleRate / kBlockSize);
    std::vector<int16_t> audio;
    audio.reserve(size_t(total_blocks) * kBlockSize * 2);
    float  buf[kNumOutputs][kBlockSize];
    float* outp[kNumOutputs] = {buf[0], buf[1], buf[2], buf[3]};
    size_t next_event        = 0;
    float  peak              = 0.f;
    std::vector<uint8_t> midi_trace;
    const int l = pair == 0 ? 0 : 2, r = pair == 0 ? 1 : 3;
    auto wall_start = std::chrono::steady_clock::now();
    for(uint64_t blk = 0; blk < total_blocks; blk++)
    {
        double t = double(blk * kBlockSize) / kSampleRate;
        if(realtime && (blk % uint64_t(burst)) == 0)
            std::this_thread::sleep_until(wall_start + std::chrono::microseconds(int64_t(t * 1e6)));
        while(next_event < events.size() && events[next_event].t <= t)
            events[next_event++].fn();
        sim.RenderBlock(nullptr, outp);
        if(trace)
        {
            // print the LED state and MIDI output whenever they change
            static std::string last_leds;
            std::string        cur = LedSnapshot(t);
            std::string        body = cur.substr(cur.find(' '));
            if(body != last_leds)
            {
                printf("%s\n", cur.c_str());
                last_leds = body;
            }
            auto mo = sim.TakeMidiOut();
            if(!mo.empty())
            {
                printf("t=%.3f midi:", t);
                for(auto b : mo)
                    printf(" %02X", b);
                printf("\n");
                midi_trace.insert(midi_trace.end(), mo.begin(), mo.end());
            }
        }
        for(int i = 0; i < kBlockSize; i++)
        {
            float a = buf[l][i], b = buf[r][i];
            peak    = std::max(peak, std::max(std::fabs(a), std::fabs(b)));
            audio.push_back(int16_t(std::lround(std::max(-1.f, std::min(1.f, a)) * 32767.f)));
            audio.push_back(int16_t(std::lround(std::max(-1.f, std::min(1.f, b)) * 32767.f)));
        }
    }
    while(next_event < events.size())
        events[next_event++].fn();

    std::string final_leds = LedSnapshot(seconds);
    if(leds_file.is_open())
        leds_file << final_leds << "\n";
    if(!ppm_path.empty())
        RenderPanel(ppm_path);
    if(!wav.empty())
        WriteWav(wav, audio, kSampleRate);
    auto midi = sim.TakeMidiOut();
    midi.insert(midi.begin(), midi_trace.begin(), midi_trace.end());
    if(!midi_path.empty())
    {
        std::ofstream f(midi_path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(midi.data()), std::streamsize(midi.size()));
    }
    Stats st = sim.GetStats();
    sim.Stop();

    printf("rendered %.2f s (%llu blocks), peak %.3f, led frames %u, midi out %zu bytes, max block %.0f us\n", seconds,
           (unsigned long long)st.blocks_rendered, peak, st.led_frames, midi.size(), st.max_block_us);
    if(!midi.empty())
    {
        printf("midi out:");
        for(size_t i = 0; i < midi.size() && i < 48; i++)
            printf(" %02X", midi[i]);
        printf("%s\n", midi.size() > 48 ? " ..." : "");
    }
    printf("%s\n", final_leds.c_str());
    return 0;
}
