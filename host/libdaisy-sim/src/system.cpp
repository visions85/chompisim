#include "sys/system.h"
#include "device.h"
using namespace daisy;
using chompi_sim::dev::Device;

void     System::Delay(uint32_t ms) { Device::Get().Delay(ms); }
void     System::DelayUs(uint32_t us) { Device::Get().DelayUs(us); }
void     System::DelayTicks(uint32_t ticks) { (void)ticks; }
uint32_t System::GetNow()
{
    Device& d = Device::Get();
    d.OnGetNow();
    return d.NowMs();
}
uint32_t System::GetUs() { return Device::Get().NowUs(); }
uint32_t System::GetTick() { return Device::Get().NowUs(); }
uint32_t System::GetTickFreq() { return 1000000; }
