#pragma once
#include <stdint.h>
#include "daisy_core.h"
namespace daisy
{
/** On the host, "disabling interrupts" takes the simulator's global recursive
 *  lock that the audio / timer thread holds while it runs firmware callbacks. */
class ScopedIrqBlocker
{
  public:
    ScopedIrqBlocker() { chompi_sim_irq_disable(); }
    ~ScopedIrqBlocker() { chompi_sim_irq_enable(); }
};
} // namespace daisy
