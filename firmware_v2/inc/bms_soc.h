/**
 * @file bms_soc.h
 * @brief SoC estimation — coulomb counting + OCV reset
 *
 * Street Smart Edition.
 */

#ifndef BMS_SOC_H
#define BMS_SOC_H

#include "bms_types.h"

void     bms_soc_init(uint16_t initial_soc_hundredths);
void     bms_soc_update(bms_pack_data_t *pack, uint32_t dt_ms);
uint16_t bms_soc_from_ocv(uint16_t cell_mv);
uint16_t bms_soc_get(void);

#endif /* BMS_SOC_H */
