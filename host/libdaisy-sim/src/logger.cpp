#include "hid/logger.h"
#include "device.h"
#include <cstdio>
namespace daisy
{
void chompi_sim_log_vprint(const char* format, va_list va, bool newline)
{
    char buf[512];
    vsnprintf(buf, sizeof(buf), format, va);
    chompi_sim::dev::Device::Get().Log(buf, newline);
}
} // namespace daisy
