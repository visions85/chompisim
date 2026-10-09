#include "device.h"
using chompi_sim::dev::Device;
using chompi_sim::dev::t_irq_depth;

extern "C" void chompi_sim_irq_disable(void)
{
    Device::Get().irq_mutex.lock();
    t_irq_depth++;
}
extern "C" void chompi_sim_irq_enable(void)
{
    if(t_irq_depth > 0)
    {
        t_irq_depth--;
        Device::Get().irq_mutex.unlock();
    }
}
extern "C" uint32_t chompi_sim_irq_primask(void) { return t_irq_depth > 0 ? 1u : 0u; }
