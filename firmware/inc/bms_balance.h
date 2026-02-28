/**
 * bms_balance.h — Passive cell balancing via BQ76952
 *
 * Balancing strategy: when max_cell - min_cell > threshold,
 * bleed cells above (min_cell + threshold/2).
 * Only balance in READY/CONNECTED at low current (<0.2C).
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_BALANCE_H
#define BMS_BALANCE_H

#include "bms_types.h"

/* ── Configuration ─────────────────────────────────────────────────── */
#define BMS_BALANCE_THRESHOLD_MV   20U   /* start balancing above 20mV delta */

/* ── Balance state ─────────────────────────────────────────────────── */
typedef struct {
    uint16_t cell_mask[BMS_NUM_MODULES];  /* bitmask of cells being balanced */
    bool     active;                       /* balancing currently active */
} bms_balance_state_t;

/**
 * Initialize balancing subsystem.
 */
void bms_balance_init(bms_balance_state_t *bal);

/**
 * Run one balancing cycle. Call from monitor step.
 * @param bal   balance state
 * @param pack  pack data (reads cell voltages, mode, current)
 */
void bms_balance_run(bms_balance_state_t *bal, const bms_pack_data_t *pack);

/**
 * HAL function: set cell balancing mask on BQ76952 module.
 * @param module_id  module index (0..BMS_NUM_MODULES-1)
 * @param cell_mask  bitmask of cells to balance (bit 0 = cell 0)
 */
void bms_hal_bq76952_set_balance(uint8_t module_id, uint16_t cell_mask);

#endif /* BMS_BALANCE_H */
