/**
 * bms_protection.h — Per-cell fault detection with leaky integrator timers
 *
 * Per-cell OV/UV fault timers: uint32_t ov_timer_ms[BMS_SE_PER_PACK]
 * Leaky integrator: increment by dt_ms when active, decay by dt_ms/2 when clear
 * HW safety runs independently of SW fault state.
 *
 * Reference: Orca ESS Integrator Manual Table 13, §6.2, §6.3
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_PROTECTION_H
#define BMS_PROTECTION_H

#include "bms_types.h"

/* ── Protection state (static allocation) ──────────────────────────── */
typedef struct {
    /* Per-cell leaky integrator timers (ms) */
    uint32_t ov_timer_ms[BMS_SE_PER_PACK];
    uint32_t uv_timer_ms[BMS_SE_PER_PACK];

    /* Per-sensor temperature timers (ms) */
    uint32_t ot_timer_ms[BMS_TOTAL_TEMP_SENSORS];

    /* HW safety timers (ms) — independent of SW */
    uint32_t hw_ov_timer_ms;
    uint32_t hw_uv_timer_ms;
    uint32_t hw_ot_timer_ms;

    /* Overcurrent timers (ms) */
    uint32_t oc_charge_timer_ms;
    uint32_t oc_discharge_timer_ms;

    /* Safe-state accumulator for fault reset (ms) */
    uint32_t safe_state_ms;

    /* Warning leaky integrator timers (ms) — 5s delay like faults */
    uint32_t warn_ov_timer_ms;
    uint32_t warn_uv_timer_ms;
    uint32_t warn_ot_timer_ms;

    /* Warning hold timer (ms) — warning stays asserted for at least 10s */
    uint32_t warning_hold_ms;

    /* Warning latched state (for hysteresis) */
    bool warn_ov_active;
    bool warn_uv_active;
    bool warn_ot_active;
} bms_protection_state_t;

/**
 * Initialize all protection timers to zero.
 */
void bms_protection_init(bms_protection_state_t *prot);

/**
 * Set NVM context for fault logging. Pass NULL to disable.
 * Forward-declared; include bms_nvm.h for the full type.
 */
void bms_protection_set_nvm(void *nvm);

/**
 * Run protection checks for one cycle.
 * @param prot   protection state
 * @param pack   pack data (reads cell_mv[], temps, current; writes faults)
 * @param dt_ms  time since last call in milliseconds
 */
void bms_protection_run(bms_protection_state_t *prot,
                        bms_pack_data_t *pack,
                        uint32_t dt_ms);

/**
 * HW safety check — runs independently of SW fault state.
 * Called from bms_protection_run but exposed for testing.
 */
void bms_protection_hw_safety(bms_protection_state_t *prot,
                               bms_pack_data_t *pack,
                               uint32_t dt_ms);

/**
 * Check if conditions are safe for fault reset (§6.3.5).
 * @return true if all cells within safe thresholds for FAULT_RESET_HOLD_MS
 */
bool bms_protection_can_reset(const bms_protection_state_t *prot,
                               const bms_pack_data_t *pack);

/**
 * Reset all fault flags and timers after successful fault reset.
 */
void bms_protection_reset(bms_protection_state_t *prot,
                           bms_pack_data_t *pack);

#endif /* BMS_PROTECTION_H */
