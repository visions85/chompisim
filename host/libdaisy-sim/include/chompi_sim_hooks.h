/** Hooks the simulator build patches into the firmware entry point. */
#pragma once
/** True until the simulator asks the firmware main loop to exit. */
bool chompi_sim_running();
/** The firmware's main(), renamed by the simulator patch. */
int chompi_firmware_main(void);
/** 64 MB of zeroed host memory standing in for the Daisy's SDRAM bank. Firmware
 *  that addresses SDRAM through a base-address variable is patched to use this
 *  instead of 0xC0000000. On Linux the block usually sits at that very address;
 *  on macOS it cannot, and nothing depends on where it is. */
void* chompi_sim_sdram(void);
