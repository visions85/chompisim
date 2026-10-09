/** Self-test of the simulator's device models, independent of the firmware UI:
 *  FatFs on the card folder, the CD4021 button chain through libDaisy's driver,
 *  the quadrature encoders through CHOMPI's encoder debouncer, and the MP2722. */
#include "chompi_sim/sim.h"
#include "daisy_seed.h"
#include "encoder.h"
#include "ff.h"
#include "wav.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace daisy;
using namespace chompi_sim;

static int fails = 0;
#define CHECK(cond, ...)                                      \
    do                                                        \
    {                                                         \
        if(!(cond))                                           \
        {                                                     \
            fails++;                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);       \
            printf(__VA_ARGS__);                              \
            printf("\n");                                     \
        }                                                     \
    } while(0)

/** Advance the device clock by rendering audio blocks: in lockstep mode the
 *  firmware sees System::GetNow() move with the sample clock, so the test is
 *  deterministic and independent of host load. */
static void AdvanceMs(int ms)
{
    float  buf[kNumOutputs][kBlockSize];
    float* outp[kNumOutputs] = {buf[0], buf[1], buf[2], buf[3]};
    for(int i = 0; i < ms * kSampleRate / 1000 / kBlockSize; i++)
        Sim::Get().RenderBlock(nullptr, outp);
}

/** A 16-bit stereo PCM WAV: a sine of `hz` at amplitude 0.5 on the left and 0.25 on the right. */
static void WriteTestWav(const std::string& path, int rate, int frames, float hz)
{
    std::vector<uint8_t> d;
    auto                 u32 = [&](uint32_t v) { for(int i = 0; i < 4; i++) d.push_back(uint8_t(v >> (8 * i))); };
    auto                 u16 = [&](uint16_t v) { d.push_back(uint8_t(v)); d.push_back(uint8_t(v >> 8)); };
    auto                 tag = [&](const char* s) { d.insert(d.end(), s, s + 4); };
    tag("RIFF"); u32(36 + uint32_t(frames) * 4); tag("WAVE");
    tag("fmt "); u32(16); u16(1); u16(2); u32(uint32_t(rate)); u32(uint32_t(rate) * 4); u16(4); u16(16);
    tag("data"); u32(uint32_t(frames) * 4);
    for(int i = 0; i < frames; i++)
    {
        float s = std::sin(2.f * 3.14159265f * hz * float(i) / float(rate));
        u16(uint16_t(int16_t(std::lround(s * 0.5f * 32767.f))));
        u16(uint16_t(int16_t(std::lround(s * 0.25f * 32767.f))));
    }
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}

int main(int argc, char** argv)
{
    Config cfg;
    cfg.card_dir = argc > 1 ? argv[1] : "card";
    cfg.realtime = false; // sample-clock time, no firmware thread: deterministic
    cfg.verbose  = false;
    if(!Sim::Get().Init(cfg))
        return 1;

    // ---- FatFs ----
    FATFS fs;
    CHECK(f_mount(&fs, "0:/", 1) == FR_OK, "mount");
    DIR     dir;
    FILINFO fno;
    int     wavs = 0;
    CHECK(f_opendir(&dir, "/") == FR_OK, "opendir");
    while(f_readdir(&dir, &fno) == FR_OK && fno.fname[0])
        if(strstr(fno.fname, ".wav"))
            wavs++;
    f_closedir(&dir);
    printf("wav files on card: %d\n", wavs);
    CHECK(wavs == 7, "expected 7 wavetables, found %d", wavs);
    FIL f;
    CHECK(f_open(&f, "WAVETABLE01.WAV", FA_OPEN_ALWAYS | FA_WRITE | FA_READ) == FR_OK, "case-insensitive open");
    CHECK(f_size(&f) > 136, "size %u", (unsigned)f_size(&f));
    CHECK(f_lseek(&f, 136) == FR_OK, "seek");
    static float table[33 * 2048];
    UINT         br = 0;
    CHECK(f_read(&f, table, sizeof(table), &br) == FR_OK && br == sizeof(table), "read %u", br);
    float peak = 0;
    for(size_t i = 0; i < 33 * 2048; i++)
        peak = std::max(peak, std::fabs(table[i]));
    printf("wavetable01 peak %.3f, tell %u size %u\n", peak, (unsigned)f_tell(&f), (unsigned)f_size(&f));
    CHECK(peak > 0.1f && peak <= 1.0f, "wavetable data looks wrong");
    f_close(&f);
    FIL w;
    CHECK(f_open(&w, "selftest_tmp.txt", FA_CREATE_ALWAYS | FA_WRITE | FA_READ) == FR_OK, "create");
    UINT bw;
    f_write(&w, "hello world", 11, &bw);
    f_lseek(&w, 5);
    f_truncate(&w);
    f_sync(&w);
    CHECK(f_size(&w) == 5, "truncate size %u", (unsigned)f_size(&w));
    f_close(&w);
    CHECK(f_rename("selftest_tmp.txt", "selftest_tmp2.txt") == FR_OK, "rename");
    CHECK(f_stat("selftest_tmp2.txt", nullptr) == FR_OK, "stat");
    CHECK(f_unlink("selftest_tmp2.txt") == FR_OK, "unlink");
    CHECK(f_stat("selftest_tmp2.txt", nullptr) == FR_NO_FILE, "gone");

    // ---- button chain through libDaisy's CD4021 driver (same config as hardware.h) ----
    ShiftRegister4021<5, 1>         sr;
    ShiftRegister4021<5, 1>::Config src;
    src.clk      = seed::D8;
    src.latch    = seed::D7;
    src.data[0]  = seed::D9;
    src.dbc_size = 7;
    sr.Init(src);
    for(int i = 0; i < 20; i++)
    {
        sr.Update();
        AdvanceMs(2);
    }
    int pressed = 0;
    for(int i = 0; i < NUM_BUTTONS; i++)
        pressed += sr.State(i);
    CHECK(pressed == 0, "%d inputs read pressed while idle", pressed);
    Sim::Get().SetButton(KEY_1, true);
    Sim::Get().SetToggle(true);
    for(int i = 0; i < 20; i++)
    {
        sr.Update();
        AdvanceMs(2);
    }
    CHECK(sr.State(KEY_1), "KEY_1 not seen pressed");
    CHECK(sr.State(SW_TOG), "toggle not seen down");
    CHECK(!sr.State(KEY_2), "KEY_2 wrongly pressed");
    Sim::Get().SetButton(KEY_1, false);
    for(int i = 0; i < 20; i++)
    {
        sr.Update();
        AdvanceMs(2);
    }
    CHECK(!sr.State(KEY_1), "KEY_1 stuck");

    // ---- encoders ----
    ShiftRegister4021<1, 1>         esr;
    ShiftRegister4021<1, 1>::Config esrc;
    esrc.clk      = seed::D22;
    esrc.latch    = seed::D23;
    esrc.data[0]  = seed::D19;
    esrc.dbc_size = 50;
    esr.Init(esrc);
    chompi::ChompiEncoder enc[6];
    for(int i = 0; i < 4; i++)
        enc[i].Init(Pin(), Pin(), Pin());
    enc[4].Init(seed::D0, seed::D20, seed::D10);
    enc[5].Init(seed::D15, seed::D17, Pin());
    int counts[6] = {0, 0, 0, 0, 0, 0};
    Sim::Get().TurnEncoder(0, 3);
    Sim::Get().TurnEncoder(3, -2);
    Sim::Get().TurnEncoder(4, 5);
    Sim::Get().TurnEncoder(5, -4);
    // the encoder phases advance inside RenderBlock; sample them every millisecond
    for(int i = 0; i < 400; i++)
    {
        AdvanceMs(1);
        esr.Update();
        for(int e = 0; e < 4; e++)
            enc[e].Debounce(esr.RawState(e * 2), esr.RawState(e * 2 + 1));
        enc[4].Debounce();
        enc[5].Debounce();
        for(int e = 0; e < 6; e++)
            counts[e] += enc[e].Increment();
    }
    printf("encoder counts: %d %d %d %d %d %d\n", counts[0], counts[1], counts[2], counts[3], counts[4], counts[5]);
    CHECK(counts[0] == 3 && counts[1] == 0 && counts[3] == -2 && counts[4] == 5 && counts[5] == -4, "encoder counts wrong");

    // ---- encoder push on GPIO ----
    Sim::Get().SetEncoderPressed(4, true);
    for(int i = 0; i < 12; i++)
    {
        enc[4].Debounce();
        AdvanceMs(2);
    }
    CHECK(enc[4].Pressed(), "SW5 push not seen");
    Sim::Get().SetEncoderPressed(4, false);

    // ---- charger model ----
    I2CHandle         i2c;
    I2CHandle::Config ic;
    ic.periph = I2CHandle::Config::Peripheral::I2C_1;
    ic.mode   = I2CHandle::Config::Mode::I2C_MASTER;
    ic.speed  = I2CHandle::Config::Speed::I2C_100KHZ;
    ic.address = 0x3F;
    i2c.Init(ic);
    uint8_t reg = 0x11, st[6] = {};
    i2c.TransmitBlocking(0x3F, &reg, 1, 10);
    i2c.ReceiveBlocking(0x3F | 0x80, st, 6, 10);
    CHECK(((st[1] >> 6) & 1) == 1, "VIN_GD should read plugged in");
    CHECK(((st[2] >> 5) & 7) == 5, "CHG_STAT should read full");

    // ---- audio input feed: a 44.1 kHz WAV is read, resampled to 48 kHz and played into the inputs ----
    {
        const std::string wav = (std::filesystem::temp_directory_path() / "chompi-selftest-tone.wav").string();
        WriteTestWav(wav, 44100, 22050, 1000.f); // 0.5 s of 1 kHz
        WavClip     clip;
        std::string err;
        CHECK(LoadWav(wav, clip, err), "LoadWav: %s", err.c_str());
        CHECK(clip.rate == 44100 && clip.channels == 2 && clip.left.size() == 22050, "wav header: %d Hz, %d ch, %zu frames", clip.rate, clip.channels, clip.left.size());
        ResampleWav(clip, kSampleRate);
        CHECK(clip.left.size() == 24000, "resampled length %zu", clip.left.size());
        int   crossings = 0;
        float peak_l = 0.f, peak_r = 0.f;
        for(size_t i = 1; i < clip.left.size(); i++)
        {
            crossings += (clip.left[i - 1] < 0.f) != (clip.left[i] < 0.f);
            peak_l = std::max(peak_l, std::fabs(clip.left[i]));
            peak_r = std::max(peak_r, std::fabs(clip.right[i]));
        }
        CHECK(crossings >= 998 && crossings <= 1002, "1 kHz sine after resampling: %d zero crossings in 0.5 s", crossings);
        CHECK(std::fabs(peak_l - 0.5f) < 0.02f && std::fabs(peak_r - 0.25f) < 0.02f, "peaks %.3f %.3f", peak_l, peak_r);

        CHECK(Sim::Get().LoadInputFile(wav, err), "LoadInputFile: %s", err.c_str());
        InputState st = Sim::Get().GetInputState();
        CHECK(std::fabs(st.length_s - 0.5) < 0.001 && !st.playing, "clip length %.4f s", st.length_s);
        CHECK(!st.line_in, "the aux jack starts unplugged");
        Sim::Get().SetLineIn(true);
        CHECK(Sim::Get().GetInputState().line_in, "aux jack plugged");
        Sim::Get().PlayInput(false, 1.f);
        AdvanceMs(200);
        st = Sim::Get().GetInputState();
        CHECK(st.playing && std::fabs(st.position_s - 0.2) < 0.002, "position after 200 ms: %.3f s", st.position_s);
        AdvanceMs(400);
        CHECK(!Sim::Get().GetInputState().playing, "a one-shot clip stops at its end");
        Sim::Get().PlayInput(true, 1.f);
        AdvanceMs(700);
        st = Sim::Get().GetInputState();
        CHECK(st.playing && std::fabs(st.position_s - 0.2) < 0.002, "looped position after 700 ms: %.3f s", st.position_s);
        Sim::Get().StopInput();
        Sim::Get().SetLineIn(false);
        std::filesystem::remove(wav);
        printf("input feed: %d crossings, peaks %.2f %.2f, length %.3f s\n", crossings, peak_l, peak_r, st.length_s);
    }

    printf(fails ? "SELFTEST FAILED (%d)\n" : "SELFTEST OK\n", fails);
    return fails ? 1 : 0;
}
