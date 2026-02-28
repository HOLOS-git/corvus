/**
 * bms_protection.c — Per-cell fault detection with leaky integrator timers
 *
 * Per-cell OV/UV: each SE has its own timer.
 * Leaky integrator: when condition active, timer += dt_ms.
 *                   when condition clear, timer -= dt_ms / 2 (but >= 0).
 * When timer >= threshold_ms → fault.
 *
 * HW safety runs independently of SW fault state (§6.2).
 * Fault latching with safe-state accumulation for reset (§6.3.5).
 *
 * Reference: Orca ESS Integrator Manual Table 13, §6.2, §6.3
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include "bms_protection.h"
#include "bms_nvm.h"
#include "bms_config.h"
#include <string.h>

/* Global NVM context for fault logging — initialized externally */
static bms_nvm_ctx_t *s_nvm_ctx;

void bms_protection_set_nvm(void *nvm)
{
    s_nvm_ctx = (bms_nvm_ctx_t *)nvm;
}

static void log_fault_to_nvm(uint32_t timestamp, uint8_t type,
                              uint8_t cell, uint16_t val)
{
    if (s_nvm_ctx != NULL) {
        bms_nvm_log_fault(s_nvm_ctx, timestamp, type, cell, val);
    }
}

/* ── Leaky integrator helpers ──────────────────────────────────────── */

static void leak_increment(uint32_t *timer, uint32_t dt_ms)
{
    /* Overflow guard */
    if (*timer <= (0xFFFFFFFFU - dt_ms)) {
        *timer += dt_ms;
    } else {
        *timer = 0xFFFFFFFFU;
    }
}

static void leak_decay(uint32_t *timer, uint32_t dt_ms)
{
    uint32_t decay = dt_ms >> BMS_LEAK_DECAY_SHIFT; /* dt/2 */
    if (*timer > decay) {
        *timer -= decay;
    } else {
        *timer = 0U;
    }
}

/* ── Init ──────────────────────────────────────────────────────────── */

void bms_protection_init(bms_protection_state_t *prot)
{
    memset(prot, 0, sizeof(*prot));
}

/* ── HW safety — independent of SW fault state ────────────────────── */

void bms_protection_hw_safety(bms_protection_state_t *prot,
                               bms_pack_data_t *pack,
                               uint32_t dt_ms)
{
    uint16_t i;

    /* HW OV: any cell >= HW_OV_MV for HW_OV_DELAY_MS */
    {
        bool any_hw_ov = false;
        for (i = 0U; i < BMS_SE_PER_PACK; i++) {
            if (pack->cell_mv[i] >= BMS_HW_OV_MV) {
                any_hw_ov = true;
                break;
            }
        }
        if (any_hw_ov) {
            leak_increment(&prot->hw_ov_timer_ms, dt_ms);
            if (prot->hw_ov_timer_ms >= BMS_HW_OV_DELAY_MS) {
                pack->faults.hw_ov = 1U;
                pack->fault_latched = true;
            }
        } else {
            leak_decay(&prot->hw_ov_timer_ms, dt_ms);
        }
    }

    /* HW UV: any cell <= HW_UV_MV for HW_UV_DELAY_MS */
    {
        bool any_hw_uv = false;
        for (i = 0U; i < BMS_SE_PER_PACK; i++) {
            if (pack->cell_mv[i] > 0U && pack->cell_mv[i] <= BMS_HW_UV_MV) {
                any_hw_uv = true;
                break;
            }
        }
        if (any_hw_uv) {
            leak_increment(&prot->hw_uv_timer_ms, dt_ms);
            if (prot->hw_uv_timer_ms >= BMS_HW_UV_DELAY_MS) {
                pack->faults.hw_uv = 1U;
                pack->fault_latched = true;
            }
        } else {
            leak_decay(&prot->hw_uv_timer_ms, dt_ms);
        }
    }

    /* HW OT: any sensor >= HW_OT for HW_OT_DELAY_MS */
    {
        bool any_hw_ot = false;
        uint8_t mod, sens;
        for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
            for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
                if (pack->modules[mod].temp_deci_c[sens] >= BMS_HW_OT_DECI_C) {
                    any_hw_ot = true;
                    break;
                }
            }
            if (any_hw_ot) { break; }
        }
        if (any_hw_ot) {
            leak_increment(&prot->hw_ot_timer_ms, dt_ms);
            if (prot->hw_ot_timer_ms >= BMS_HW_OT_DELAY_MS) {
                pack->faults.hw_ot = 1U;
                pack->fault_latched = true;
            }
        } else {
            leak_decay(&prot->hw_ot_timer_ms, dt_ms);
        }
    }
}

/* ── Main protection run ───────────────────────────────────────────── */

void bms_protection_run(bms_protection_state_t *prot,
                        bms_pack_data_t *pack,
                        uint32_t dt_ms)
{
    uint16_t i;
    uint8_t mod, sens;

    /* HW safety ALWAYS runs, even if fault_latched */
    bms_protection_hw_safety(prot, pack, dt_ms);

    /* If already fault-latched, only accumulate safe-state time */
    if (pack->fault_latched) {
        /* Check if ALL cells are within safe range */
        bool all_safe = true;
        for (i = 0U; i < BMS_SE_PER_PACK; i++) {
            if (pack->cell_mv[i] >= BMS_SE_OV_FAULT_MV ||
                (pack->cell_mv[i] > 0U && pack->cell_mv[i] <= BMS_SE_UV_FAULT_MV)) {
                all_safe = false;
                break;
            }
        }
        if (all_safe && pack->max_temp_deci_c < BMS_SE_OT_FAULT_DECI_C) {
            leak_increment(&prot->safe_state_ms, dt_ms);
        } else {
            prot->safe_state_ms = 0U;
        }
        return;
    }

    /* ── Per-cell OV check ─────────────────────────────────────────── */
    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        if (pack->cell_mv[i] >= BMS_SE_OV_FAULT_MV) {
            leak_increment(&prot->ov_timer_ms[i], dt_ms);
            if (prot->ov_timer_ms[i] >= BMS_SE_FAULT_DELAY_MS) {
                pack->faults.cell_ov = 1U;
                pack->fault_latched = true;
                log_fault_to_nvm(pack->uptime_ms, 1U, (uint8_t)i, pack->cell_mv[i]);
                BMS_LOG("OV fault: cell %u = %u mV", i, pack->cell_mv[i]);
                return;
            }
        } else {
            leak_decay(&prot->ov_timer_ms[i], dt_ms);
        }
    }

    /* ── Per-cell UV check ─────────────────────────────────────────── */
    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        /* Skip cells reading 0 (likely unconnected) */
        if (pack->cell_mv[i] == 0U) { continue; }
        if (pack->cell_mv[i] <= BMS_SE_UV_FAULT_MV) {
            leak_increment(&prot->uv_timer_ms[i], dt_ms);
            if (prot->uv_timer_ms[i] >= BMS_SE_FAULT_DELAY_MS) {
                pack->faults.cell_uv = 1U;
                pack->fault_latched = true;
                log_fault_to_nvm(pack->uptime_ms, 2U, (uint8_t)i, pack->cell_mv[i]);
                BMS_LOG("UV fault: cell %u = %u mV", i, pack->cell_mv[i]);
                return;
            }
        } else {
            leak_decay(&prot->uv_timer_ms[i], dt_ms);
        }
    }

    /* ── Per-sensor OT check ───────────────────────────────────────── */
    {
        uint16_t sensor_idx = 0U;
        for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
            for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
                int16_t t = pack->modules[mod].temp_deci_c[sens];
                if (t >= BMS_SE_OT_FAULT_DECI_C) {
                    leak_increment(&prot->ot_timer_ms[sensor_idx], dt_ms);
                    if (prot->ot_timer_ms[sensor_idx] >= BMS_SE_FAULT_DELAY_MS) {
                        pack->faults.cell_ot = 1U;
                        pack->fault_latched = true;
                        log_fault_to_nvm(pack->uptime_ms, 3U, (uint8_t)sensor_idx, (uint16_t)t);
                        BMS_LOG("OT fault: sensor %u = %d deci-C", sensor_idx, t);
                        return;
                    }
                } else {
                    leak_decay(&prot->ot_timer_ms[sensor_idx], dt_ms);
                }
                sensor_idx++;
            }
        }
    }

    /* ── Overcurrent check ─────────────────────────────────────────── */
    if (pack->pack_current_ma > BMS_MAX_CHARGE_MA) {
        leak_increment(&prot->oc_charge_timer_ms, dt_ms);
        if (prot->oc_charge_timer_ms >= BMS_SE_FAULT_DELAY_MS) {
            pack->faults.oc_charge = 1U;
            pack->fault_latched = true;
        }
    } else {
        leak_decay(&prot->oc_charge_timer_ms, dt_ms);
    }

    if (pack->pack_current_ma < -BMS_MAX_DISCHARGE_MA) {
        leak_increment(&prot->oc_discharge_timer_ms, dt_ms);
        if (prot->oc_discharge_timer_ms >= BMS_SE_FAULT_DELAY_MS) {
            pack->faults.oc_discharge = 1U;
            pack->fault_latched = true;
        }
    } else {
        leak_decay(&prot->oc_discharge_timer_ms, dt_ms);
    }

    /* ── Warning check (OV/UV/OT below fault but above warning) ───── */
    {
        bool warn = false;
        for (i = 0U; i < BMS_SE_PER_PACK; i++) {
            if (pack->cell_mv[i] >= BMS_SE_OV_WARN_MV) { warn = true; break; }
            if (pack->cell_mv[i] > 0U && pack->cell_mv[i] <= BMS_SE_UV_WARN_MV) {
                warn = true; break;
            }
        }
        if (!warn && pack->max_temp_deci_c >= BMS_SE_OT_WARN_DECI_C) {
            warn = true;
        }
        pack->has_warning = warn;
    }
}

/* ── Can reset check ───────────────────────────────────────────────── */

bool bms_protection_can_reset(const bms_protection_state_t *prot,
                               const bms_pack_data_t *pack)
{
    if (!pack->fault_latched) {
        return true;
    }
    /* Must be in safe range for FAULT_RESET_HOLD_MS */
    return (prot->safe_state_ms >= BMS_FAULT_RESET_HOLD_MS);
}

/* ── Reset ─────────────────────────────────────────────────────────── */

void bms_protection_reset(bms_protection_state_t *prot,
                           bms_pack_data_t *pack)
{
    memset(prot, 0, sizeof(*prot));
    memset(&pack->faults, 0, sizeof(pack->faults));
    pack->fault_latched = false;
    pack->has_warning = false;
}
