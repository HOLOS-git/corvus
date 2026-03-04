/**
 * @file bms_current_limit.h
 * @brief Temperature/SoC/SEV current derating (§7.4)
 *
 * Street Smart Edition.
 */

#ifndef BMS_CURRENT_LIMIT_H
#define BMS_CURRENT_LIMIT_H

#include "bms_types.h"

void bms_current_limit_compute(const bms_pack_data_t *pack,
                                int32_t *max_charge_ma,
                                int32_t *max_discharge_ma);

#endif /* BMS_CURRENT_LIMIT_H */
