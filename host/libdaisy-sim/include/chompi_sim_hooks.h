/** Hooks the simulator build patches into the firmware entry point. */
#pragma once
/** True until the simulator asks the firmware main loop to exit. */
bool chompi_sim_running();
/** The firmware's main(), renamed by the simulator patch. */
int chompi_firmware_main(void);
