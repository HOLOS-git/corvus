/**
 * bms_current_limit.h — Temperature/SoC/SEV current derating (§7.4)
 *
 * Ports Figure 28/29/30 breakpoints from Python simulation to fixed-point.
 * All temperatures in deci-°C, currents in milliamps, SoC in hundredths %.
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_CURRENT_LIMIT_H
#define BMS_CURRENT_LIMIT_H

#include "bms_types.h"

/**
 * Compute current limits from temperature, SoC, and SEV derating.
 * Takes the minimum of all three sources.
 *
 * @param pack             pack data (reads temps, soc, cell voltages)
 * @param max_charge_ma    output: maximum charge current (mA, positive)
 * @param max_discharge_ma output: maximum discharge current (mA, positive)
 */
void bms_current_limit_compute(
    const bms_pack_data_t *pack,
    int32_t *max_charge_ma,
    int32_t *max_discharge_ma
);

#endif /* BMS_CURRENT_LIMIT_H */
