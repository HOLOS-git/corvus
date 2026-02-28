/**
 * bms_soc.h — State of Charge estimation (coulomb counting + OCV reset)
 *
 * SoC stored as uint16_t in hundredths of percent (0–10000 = 0.00%–100.00%).
 * 24-point OCV table ported from Python simulation.
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_SOC_H
#define BMS_SOC_H

#include "bms_types.h"

/**
 * Initialize SoC subsystem.
 * @param initial_soc_hundredths  starting SoC (0–10000)
 */
void bms_soc_init(uint16_t initial_soc_hundredths);

/**
 * Update SoC via coulomb counting.
 * @param pack   pack data (reads pack_current_ma; writes soc_hundredths)
 * @param dt_ms  time since last call in milliseconds
 */
void bms_soc_update(bms_pack_data_t *pack, uint32_t dt_ms);

/**
 * Look up SoC from cell OCV (millivolts).
 * Uses 24-point NMC 622 OCV table.
 * @param cell_mv  open-circuit voltage in millivolts
 * @return SoC in hundredths of percent (0–10000)
 */
uint16_t bms_soc_from_ocv(uint16_t cell_mv);

/**
 * Get current SoC value.
 * @return SoC in hundredths of percent
 */
uint16_t bms_soc_get(void);

#endif /* BMS_SOC_H */
