/**
 * @file bms_balance.h
 * @brief Passive cell balancing via BQ76952
 *
 * Street Smart Edition.
 */

#ifndef BMS_BALANCE_H
#define BMS_BALANCE_H

#include "bms_types.h"

typedef struct {
    uint16_t cell_mask[BMS_NUM_MODULES];
    bool     active;
} bms_balance_state_t;

void bms_balance_init(bms_balance_state_t *bal);
void bms_balance_run(bms_balance_state_t *bal, const bms_pack_data_t *pack);

#endif /* BMS_BALANCE_H */
