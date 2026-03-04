/**
 * @file bms_protection.h
 * @brief Protection system — SW + HW layers, dT/dt, sensor fault
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P0-01: Sensor fault detection (Dave, Priya, Catherine, Yara)
 *   P0-02: comm_loss → fault_latched (Dave, Catherine)
 *   P0-04: dT/dt thermal rate-of-rise (Catherine, Mikael, Henrik, Priya)
 *   P0-05: Sub-zero charging hard fault (Dave)
 *   P2-05: Timer preservation across reset (Yara)
 *   P2-07: Plausibility checks (Dave, Yara, Priya)
 */

#ifndef BMS_PROTECTION_H
#define BMS_PROTECTION_H

#include "bms_types.h"

typedef struct {
    /* Per-cell leaky integrator timers */
    uint32_t ov_timer_ms[BMS_SE_PER_PACK];
    uint32_t uv_timer_ms[BMS_SE_PER_PACK];

    /* Per-sensor OT timers */
    uint32_t ot_timer_ms[BMS_TOTAL_TEMP_SENSORS];

    /* HW safety timers */
    uint32_t hw_ov_timer_ms;
    uint32_t hw_uv_timer_ms;
    uint32_t hw_ot_timer_ms;

    /* Overcurrent timers */
    uint32_t oc_charge_timer_ms;
    uint32_t oc_discharge_timer_ms;

    /* P0-05: Sub-zero charging timer */
    uint32_t subzero_charge_timer_ms;

    /* Safe-state accumulator */
    uint32_t safe_state_ms;

    /* Warning timers */
    uint32_t warn_ov_timer_ms;
    uint32_t warn_uv_timer_ms;
    uint32_t warn_ot_timer_ms;
    uint32_t warning_hold_ms;
    bool     warn_ov_active;
    bool     warn_uv_active;
    bool     warn_ot_active;

    /* P2-05: Fault reset tracking — timers NOT zeroed on reset */
    uint8_t  reset_count_this_hour;
    uint32_t reset_hour_start_ms;
    uint8_t  consec_fault_count[8];  /* per fault type */
} bms_protection_state_t;

void bms_protection_init(bms_protection_state_t *prot);
void bms_protection_set_nvm(void *nvm);

/**
 * Run all protection checks for one cycle.
 * Includes P0-05 sub-zero, P2-07 plausibility, HW safety.
 */
void bms_protection_run(bms_protection_state_t *prot,
                        bms_pack_data_t *pack,
                        uint32_t dt_ms);

void bms_protection_hw_safety(bms_protection_state_t *prot,
                               bms_pack_data_t *pack,
                               uint32_t dt_ms);

/**
 * P2-05: Can reset? Checks safe-state hold AND reset rate limit.
 */
bool bms_protection_can_reset(const bms_protection_state_t *prot,
                               const bms_pack_data_t *pack);

/**
 * P2-05: Reset clears fault FLAGS but preserves integrator timers.
 * Logs reset to NVM. Tracks reset count per hour.
 */
void bms_protection_reset(bms_protection_state_t *prot,
                           bms_pack_data_t *pack);

#endif /* BMS_PROTECTION_H */
