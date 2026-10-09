#pragma once
#ifndef DSY_SYSTEM_H
#define DSY_SYSTEM_H
#include <cstdint>

namespace daisy
{
/** Host simulator version of libDaisy's System. Time comes from the simulator
 *  core: wall-clock in realtime mode, the audio sample clock in lockstep mode. */
class System
{
  public:
    struct Config
    {
        enum class SysClkFreq
        {
            FREQ_400MHZ,
            FREQ_480MHZ,
        };
        SysClkFreq cpu_freq   = SysClkFreq::FREQ_400MHZ;
        bool       use_dcache = true;
        bool       use_icache = true;
        bool       skip_clocks = false;
        void       Defaults()
        {
            cpu_freq    = SysClkFreq::FREQ_400MHZ;
            use_dcache  = true;
            use_icache  = true;
            skip_clocks = false;
        }
        void Boost()
        {
            cpu_freq    = SysClkFreq::FREQ_480MHZ;
            use_dcache  = true;
            use_icache  = true;
            skip_clocks = false;
        }
    };

    enum MemoryRegion
    {
        INTERNAL_FLASH = 0,
        ITCMRAM,
        DTCMRAM,
        SRAM_D1,
        SRAM_D2,
        SRAM_D3,
        SDRAM,
        QSPI,
        INVALID_ADDRESS,
    };

    struct BootInfo
    {
        enum class Type : uint32_t { INVALID = 0, JUMP = 0xDEADBEEF, SKIP_TIMEOUT = 0x5AFEB007, INF_TIMEOUT = 0xB0074EFA };
        enum class Version : uint32_t { LT_v6_0 = 0, NONE, v6_0, v6_1, CHOMPI, LAST };
        Type     status;
        uint32_t data;
        Version  version;
    };

    System() {}
    ~System() {}
    void          Init() {}
    void          Init(const Config& config) { cfg_ = config; }
    void          DeInit() {}
    const Config& GetConfig() const { return cfg_; }

    static void Delay(uint32_t delay_ms);
    static void DelayUs(uint32_t delay_us);
    static void DelayTicks(uint32_t delay_ticks);

    static uint32_t GetNow();
    static uint32_t GetUs();
    static uint32_t GetTick();
    static uint32_t GetTickFreq();
    static uint32_t GetSysClkFreq() { return 480000000; }
    static uint32_t GetHClkFreq() { return 240000000; }
    static uint32_t GetPClk1Freq() { return 120000000; }
    static uint32_t GetPClk2Freq() { return 120000000; }

    static MemoryRegion GetProgramMemoryRegion() { return SRAM_D1; }
    static MemoryRegion GetMemoryRegion(uint32_t address) { (void)address; return SRAM_D1; }
    static BootInfo::Version GetBootloaderVersion() { return BootInfo::Version::CHOMPI; }

  private:
    Config cfg_;
};
} // namespace daisy
#endif
