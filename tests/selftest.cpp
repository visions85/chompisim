/** Self-test of the simulator's device models, independent of the firmware UI:
 *  FatFs on the card folder, the CD4021 button chain through libDaisy's driver,
 *  the quadrature encoders through CHOMPI's encoder debouncer, and the MP2722. */
#include "chompi_sim/sim.h"
#include "daisy_seed.h"
#include "encoder.h"
#include "ff.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

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

static void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int main(int argc, char** argv)
{
    Config cfg;
    cfg.card_dir = argc > 1 ? argv[1] : "card";
    cfg.realtime = true; // wall clock, no firmware thread
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
        SleepMs(2);
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
        SleepMs(2);
    }
    CHECK(sr.State(KEY_1), "KEY_1 not seen pressed");
    CHECK(sr.State(SW_TOG), "toggle not seen down");
    CHECK(!sr.State(KEY_2), "KEY_2 wrongly pressed");
    Sim::Get().SetButton(KEY_1, false);
    for(int i = 0; i < 20; i++)
    {
        sr.Update();
        SleepMs(2);
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
    // the encoder phases advance inside RenderBlock; pump blocks like a sound card would
    float  buf[kNumOutputs][kBlockSize];
    float* outp[kNumOutputs] = {buf[0], buf[1], buf[2], buf[3]};
    for(int i = 0; i < 400; i++)
    {
        Sim::Get().RenderBlock(nullptr, outp);
        esr.Update();
        for(int e = 0; e < 4; e++)
            enc[e].Debounce(esr.RawState(e * 2), esr.RawState(e * 2 + 1));
        enc[4].Debounce();
        enc[5].Debounce();
        for(int e = 0; e < 6; e++)
            counts[e] += enc[e].Increment();
        SleepMs(1);
    }
    printf("encoder counts: %d %d %d %d %d %d\n", counts[0], counts[1], counts[2], counts[3], counts[4], counts[5]);
    CHECK(counts[0] == 3 && counts[1] == 0 && counts[3] == -2 && counts[4] == 5 && counts[5] == -4, "encoder counts wrong");

    // ---- encoder push on GPIO ----
    Sim::Get().SetEncoderPressed(4, true);
    for(int i = 0; i < 12; i++)
    {
        enc[4].Debounce();
        SleepMs(2);
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

    printf(fails ? "SELFTEST FAILED (%d)\n" : "SELFTEST OK\n", fails);
    return fails ? 1 : 0;
}
