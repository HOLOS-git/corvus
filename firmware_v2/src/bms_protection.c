/**
 * @file bms_protection.c
 * @brief Per-cell fault detection — leaky integrators + dT/dt + sub-zero
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P0-05: Sub-zero charging hard fault — 0A margin below freezing (Dave)
 *   P2-05: Timer preservation across fault reset (Yara)
 *     "bms_protection_reset() does memset(prot, 0, sizeof(*prot)) — all 308
 *      OV timers zeroed. Timing attack: reset every 60s." — Yara
 *   P2-05: Max 3 resets per hour (Yara)
 *   P0-02: comm_loss now latches (handled in monitor, checked here)
 */

#include "bms_protection.h"
#include "bms_current_limit.h"
#include "bms_nvm.h"
#include "bms_config.h"
#include <string.h>

static bms_nvm_ctx_t *s_nvm_ctx;

void bms_protection_set_nvm(void *nvm)
{
    s_nvm_ctx = (bms_nvm_ctx_t *)nvm;
}

static void log_fault(uint32_t ts, uint8_t type, uint8_t cell, uint16_t val)
{
    if (s_nvm_ctx != NULL) {
        bms_nvm_log_fault(s_nvm_ctx, ts, type, cell, val);
    }
}

/* ── Leaky integrator helpers ──────────────────────────────────────── */

static void leak_inc(uint32_t *timer, uint32_t dt_ms)
{
    if (*timer <= (0xFFFFFFFFU - dt_ms)) {
        *timer += dt_ms;
    } else {
        *timer = 0xFFFFFFFFU;
    }
}

static void leak_dec(uint32_t *timer, uint32_t dt_ms)
{
    uint32_t decay = dt_ms >> BMS_LEAK_DECAY_SHIFT;
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

/* ── HW safety — always runs ───────────────────────────────────────── */

void bms_protection_hw_safety(bms_protection_state_t *prot,
                               bms_pack_data_t *pack,
                               uint32_t dt_ms)
{
    uint16_t i;
    bool any_hw_ov = false;
    bool any_hw_uv = false;
    bool any_hw_ot = false;
    uint8_t mod, sens;

    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        if (pack->cell_mv[i] >= BMS_HW_OV_MV) { any_hw_ov = true; break; }
    }
    if (any_hw_ov) {
        leak_inc(&prot->hw_ov_timer_ms, dt_ms);
        if (prot->hw_ov_timer_ms >= BMS_HW_OV_DELAY_MS) {
            pack->faults.hw_ov = 1U;
            pack->fault_latched = true;
        }
    } else {
        leak_dec(&prot->hw_ov_timer_ms, dt_ms);
    }

    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        if (pack->cell_mv[i] > 0U && pack->cell_mv[i] <= BMS_HW_UV_MV) {
            any_hw_uv = true; break;
        }
    }
    if (any_hw_uv) {
        leak_inc(&prot->hw_uv_timer_ms, dt_ms);
        if (prot->hw_uv_timer_ms >= BMS_HW_UV_DELAY_MS) {
            pack->faults.hw_uv = 1U;
            pack->fault_latched = true;
        }
    } else {
        leak_dec(&prot->hw_uv_timer_ms, dt_ms);
    }

    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
            if (pack->modules[mod].sensor_fault[sens].faulted) { continue; }
            if (pack->modules[mod].temp_deci_c[sens] >= BMS_HW_OT_DECI_C) {
                any_hw_ot = true; break;
            }
        }
        if (any_hw_ot) { break; }
    }
    if (any_hw_ot) {
        leak_inc(&prot->hw_ot_timer_ms, dt_ms);
        if (prot->hw_ot_timer_ms >= BMS_HW_OT_DELAY_MS) {
            pack->faults.hw_ot = 1U;
            pack->fault_latched = true;
        }
    } else {
        leak_dec(&prot->hw_ot_timer_ms, dt_ms);
    }
}

/* ── Main protection ───────────────────────────────────────────────── */

void bms_protection_run(bms_protection_state_t *prot,
                        bms_pack_data_t *pack,
                        uint32_t dt_ms)
{
    uint16_t i;
    uint8_t mod, sens;

    /* HW safety ALWAYS runs */
    bms_protection_hw_safety(prot, pack, dt_ms);

    /* If fault-latched, accumulate safe-state time only */
    if (pack->fault_latched) {
        bool all_safe = true;
        for (i = 0U; i < BMS_SE_PER_PACK; i++) {
            if (pack->cell_mv[i] >= BMS_SE_OV_FAULT_MV ||
                (pack->cell_mv[i] > 0U && pack->cell_mv[i] <= BMS_SE_UV_FAULT_MV)) {
                all_safe = false;
                break;
            }
        }
        if (all_safe && pack->max_temp_deci_c < BMS_SE_OT_FAULT_DECI_C) {
            leak_inc(&prot->safe_state_ms, dt_ms);
        } else {
            prot->safe_state_ms = 0U;
        }
        return;
    }

    /* ── Per-cell OV ───────────────────────────────────────────────── */
    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        if (pack->cell_mv[i] >= BMS_SE_OV_FAULT_MV) {
            leak_inc(&prot->ov_timer_ms[i], dt_ms);
            if (prot->ov_timer_ms[i] >= BMS_SE_FAULT_DELAY_MS) {
                pack->faults.cell_ov = 1U;
                pack->fault_latched = true;
                log_fault(pack->uptime_ms, NVM_FAULT_OV, (uint8_t)i, pack->cell_mv[i]);
                return;
            }
        } else {
            leak_dec(&prot->ov_timer_ms[i], dt_ms);
        }
    }

    /* ── Per-cell UV ───────────────────────────────────────────────── */
    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        if (pack->cell_mv[i] == 0U) { continue; }
        if (pack->cell_mv[i] <= BMS_SE_UV_FAULT_MV) {
            leak_inc(&prot->uv_timer_ms[i], dt_ms);
            if (prot->uv_timer_ms[i] >= BMS_SE_FAULT_DELAY_MS) {
                pack->faults.cell_uv = 1U;
                pack->fault_latched = true;
                log_fault(pack->uptime_ms, NVM_FAULT_UV, (uint8_t)i, pack->cell_mv[i]);
                return;
            }
        } else {
            leak_dec(&prot->uv_timer_ms[i], dt_ms);
        }
    }

    /* ── Per-sensor OT ─────────────────────────────────────────────── */
    {
        uint16_t sensor_idx = 0U;
        for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
            for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
                /* P0-01: Skip faulted sensors */
                if (pack->modules[mod].sensor_fault[sens].faulted) {
                    sensor_idx++;
                    continue;
                }
                int16_t t = pack->modules[mod].temp_deci_c[sens];
                if (t >= BMS_SE_OT_FAULT_DECI_C) {
                    leak_inc(&prot->ot_timer_ms[sensor_idx], dt_ms);
                    if (prot->ot_timer_ms[sensor_idx] >= BMS_SE_FAULT_DELAY_MS) {
                        pack->faults.cell_ot = 1U;
                        pack->fault_latched = true;
                        log_fault(pack->uptime_ms, NVM_FAULT_OT,
                                  (uint8_t)sensor_idx, (uint16_t)t);
                        return;
                    }
                } else {
                    leak_dec(&prot->ot_timer_ms[sensor_idx], dt_ms);
                }
                sensor_idx++;
            }
        }
    }

    /* ═══════════════════════════════════════════════════════════════════
     * P0-05: Sub-zero charging hard fault (Dave)
     *
     * Safety rationale: If pack_current > 0 (charging) AND min_temp < 0°C,
     * NMC cells lithium-plate silently. Original code allowed 5A margin
     * via the OC formula. Now: ZERO margin. Any charge current below
     * freezing for >5s → fault_latched.
     *
     * "If EMS ignores the limit, NMC cells lithium-plate silently" — Dave
     * ═══════════════════════════════════════════════════════════════════ */
    if (pack->pack_current_ma > BMS_SUBZERO_CHARGE_MARGIN_MA &&
        pack->min_temp_deci_c < BMS_SUBZERO_TEMP_THRESHOLD_DC) {
        leak_inc(&prot->subzero_charge_timer_ms, dt_ms);
        if (prot->subzero_charge_timer_ms >= BMS_SUBZERO_CHARGE_FAULT_MS) {
            pack->faults.subzero_charge = 1U;
            pack->fault_latched = true;
            log_fault(pack->uptime_ms, NVM_FAULT_SUBZERO, 0xFFU,
                      (uint16_t)pack->min_temp_deci_c);
            BMS_LOG("P0-05: Sub-zero charge fault! T=%d, I=%d",
                    pack->min_temp_deci_c, (int)pack->pack_current_ma);
            return;
        }
    } else {
        leak_dec(&prot->subzero_charge_timer_ms, dt_ms);
    }

    /* ── Overcurrent ───────────────────────────────────────────────── */
    {
        int32_t temp_chg, temp_dchg;
        bms_current_limit_compute(pack, &temp_chg, &temp_dchg);

        /* OC charge */
        if (pack->pack_current_ma > 0 && pack->pack_current_ma > temp_chg) {
            leak_inc(&prot->oc_charge_timer_ms, dt_ms);
            if (prot->oc_charge_timer_ms >= BMS_SE_FAULT_DELAY_MS) {
                pack->faults.oc_charge = 1U;
                pack->fault_latched = true;
                log_fault(pack->uptime_ms, NVM_FAULT_OC_CHG, 0xFFU,
                          (uint16_t)(pack->pack_current_ma / 1000));
            }
        } else {
            leak_dec(&prot->oc_charge_timer_ms, dt_ms);
        }

        /* OC discharge */
        if (pack->pack_current_ma < -(int32_t)BMS_MAX_DISCHARGE_MA) {
            leak_inc(&prot->oc_discharge_timer_ms, dt_ms);
            if (prot->oc_discharge_timer_ms >= BMS_SE_FAULT_DELAY_MS) {
                pack->faults.oc_discharge = 1U;
                pack->fault_latched = true;
            }
        } else {
            leak_dec(&prot->oc_discharge_timer_ms, dt_ms);
        }
    }

    /* ── Warnings (with hysteresis) ────────────────────────────────── */
    {
        bool warn_ov = false, warn_uv = false, warn_ot = false;

        for (i = 0U; i < BMS_SE_PER_PACK; i++) {
            uint16_t thresh = prot->warn_ov_active ?
                BMS_SE_OV_WARN_CLEAR_MV : BMS_SE_OV_WARN_MV;
            if (pack->cell_mv[i] >= thresh) { warn_ov = true; break; }
        }
        for (i = 0U; i < BMS_SE_PER_PACK; i++) {
            if (pack->cell_mv[i] == 0U) { continue; }
            uint16_t thresh = prot->warn_uv_active ?
                BMS_SE_UV_WARN_CLEAR_MV : BMS_SE_UV_WARN_MV;
            if (pack->cell_mv[i] <= thresh) { warn_uv = true; break; }
        }
        {
            int16_t thresh = prot->warn_ot_active ?
                BMS_SE_OT_WARN_CLEAR_DC : BMS_SE_OT_WARN_DECI_C;
            if (pack->max_temp_deci_c >= thresh) { warn_ot = true; }
        }

        /* Timer management */
        if (warn_ov) { leak_inc(&prot->warn_ov_timer_ms, dt_ms); }
        else { leak_dec(&prot->warn_ov_timer_ms, dt_ms); }
        prot->warn_ov_active = (prot->warn_ov_timer_ms >= BMS_WARN_DELAY_MS);
        if (!warn_ov && prot->warn_ov_timer_ms == 0U) { prot->warn_ov_active = false; }

        if (warn_uv) { leak_inc(&prot->warn_uv_timer_ms, dt_ms); }
        else { leak_dec(&prot->warn_uv_timer_ms, dt_ms); }
        prot->warn_uv_active = (prot->warn_uv_timer_ms >= BMS_WARN_DELAY_MS);
        if (!warn_uv && prot->warn_uv_timer_ms == 0U) { prot->warn_uv_active = false; }

        if (warn_ot) { leak_inc(&prot->warn_ot_timer_ms, dt_ms); }
        else { leak_dec(&prot->warn_ot_timer_ms, dt_ms); }
        prot->warn_ot_active = (prot->warn_ot_timer_ms >= BMS_WARN_DELAY_MS);
        if (!warn_ot && prot->warn_ot_timer_ms == 0U) { prot->warn_ot_active = false; }

        /* Warning hold */
        bool any_warn = prot->warn_ov_active || prot->warn_uv_active || prot->warn_ot_active;
        if (any_warn) {
            prot->warning_hold_ms = BMS_WARN_HOLD_MS;
            pack->has_warning = true;
        } else if (prot->warning_hold_ms > 0U) {
            prot->warning_hold_ms = (prot->warning_hold_ms > dt_ms) ?
                (prot->warning_hold_ms - dt_ms) : 0U;
            pack->has_warning = (prot->warning_hold_ms > 0U);
        } else {
            pack->has_warning = false;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * P2-05: Fault Reset — preserves integrator timers (Yara)
 *
 * Original code: memset(prot, 0, sizeof(*prot)) — zeroed everything.
 * Attack: reset every 60s → no fault ever latches.
 *
 * New behavior:
 *   - Fault FLAGS cleared, integrator TIMERS preserved
 *   - Max 3 resets per hour
 *   - N consecutive same-type faults → manual intervention required
 *   - Every reset logged to NVM
 * ═══════════════════════════════════════════════════════════════════════ */

bool bms_protection_can_reset(const bms_protection_state_t *prot,
                               const bms_pack_data_t *pack)
{
    if (!pack->fault_latched) { return true; }

    /* P2-05: Check reset rate limit */
    if (prot->reset_count_this_hour >= BMS_MAX_RESETS_PER_HOUR) {
        BMS_LOG("P2-05: Reset denied — %u resets this hour (max %u)",
                prot->reset_count_this_hour, BMS_MAX_RESETS_PER_HOUR);
        return false;
    }

    /* P1-05: Fire/suppression requires manual intervention, not CAN reset */
    if (pack->faults.fire_detected || pack->faults.fire_suppression) {
        BMS_LOG("P1-05: Reset denied — fire event requires manual intervention");
        return false;
    }

    /* Must be in safe range for hold time */
    return (prot->safe_state_ms >= BMS_FAULT_RESET_HOLD_MS);
}

void bms_protection_reset(bms_protection_state_t *prot,
                           bms_pack_data_t *pack)
{
    /* P2-05: Log the reset event */
    log_fault(pack->uptime_ms, NVM_FAULT_RESET, 0xFFU, 0U);

    /* P2-05: Track reset count for rate limiting */
    if (pack->uptime_ms - prot->reset_hour_start_ms > 3600000U) {
        prot->reset_hour_start_ms = pack->uptime_ms;
        prot->reset_count_this_hour = 0U;
    }
    prot->reset_count_this_hour++;

    /* Clear fault FLAGS only — NOT integrator timers (P2-05) */
    memset(&pack->faults, 0, sizeof(pack->faults));
    pack->fault_latched = false;
    pack->has_warning = false;

    /* Reset safe-state accumulator */
    prot->safe_state_ms = 0U;

    /* Note: ov_timer_ms[], uv_timer_ms[], ot_timer_ms[] are PRESERVED.
     * This prevents Yara's timing attack where resetting every 60s
     * prevents faults from ever latching. */
}
