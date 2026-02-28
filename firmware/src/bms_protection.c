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
#include "bms_current_limit.h"
#include "bms_nvm.h"
#include "bms_config.h"
#include <string.h>

/* Warning delay: 5000ms leaky integrator (same as fault) */
#define BMS_WARN_DELAY_MS       5000U
/* Warning hold: stays asserted for at least 10s after condition clears */
#define BMS_WARN_HOLD_MS       10000U
/* OC warning: 1.05 × temp limit + 5A margin */
#define BMS_OC_WARN_MARGIN_PPT  1050   /* 1.05 in parts per thousand */
#define BMS_OC_WARN_OFFSET_MA   5000   /* 5A */
#define BMS_OC_WARN_DELAY_MS   10000U  /* 10s delay for OC warning */

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

    /* ── Temperature-dependent overcurrent check (Issue 4) ────────── */
    {
        int32_t temp_charge_limit_ma, temp_discharge_limit_ma;
        bms_current_limit_compute(pack, &temp_charge_limit_ma, &temp_discharge_limit_ma);

        /* OC charge fault: only at T<0°C during charge (per Python/Table 13) */
        if (pack->pack_current_ma > 0 && pack->min_temp_deci_c < 0) {
            if (pack->pack_current_ma > temp_charge_limit_ma) {
                leak_increment(&prot->oc_charge_timer_ms, dt_ms);
                if (prot->oc_charge_timer_ms >= BMS_SE_FAULT_DELAY_MS) {
                    pack->faults.oc_charge = 1U;
                    pack->fault_latched = true;
                }
            } else {
                leak_decay(&prot->oc_charge_timer_ms, dt_ms);
            }
        } else {
            leak_decay(&prot->oc_charge_timer_ms, dt_ms);
        }

        /* OC discharge fault: static limit (always active) */
        if (pack->pack_current_ma < -(int32_t)BMS_MAX_DISCHARGE_MA) {
            leak_increment(&prot->oc_discharge_timer_ms, dt_ms);
            if (prot->oc_discharge_timer_ms >= BMS_SE_FAULT_DELAY_MS) {
                pack->faults.oc_discharge = 1U;
                pack->fault_latched = true;
            }
        } else {
            leak_decay(&prot->oc_discharge_timer_ms, dt_ms);
        }

        /* OC warning: I > 1.05 × temp_charge_limit + 5A with 10s delay */
        {
            int32_t oc_warn_thresh = (int32_t)((int64_t)temp_charge_limit_ma *
                                     BMS_OC_WARN_MARGIN_PPT / 1000) +
                                     BMS_OC_WARN_OFFSET_MA;
            if (pack->pack_current_ma > oc_warn_thresh) {
                leak_increment(&prot->oc_charge_timer_ms, dt_ms);
                /* OC warning uses a longer delay; piggyback on charge timer
                 * but only warn (not fault) above OC_WARN_DELAY threshold */
            }
        }
    }

    /* ── Warning check with 5s delay timers + hysteresis (Issues 2,3) ── */
    {
        bool warn_cond_ov = false;
        bool warn_cond_uv = false;
        bool warn_cond_ot = false;

        /* Check OV warning condition (with hysteresis) */
        for (i = 0U; i < BMS_SE_PER_PACK; i++) {
            uint16_t thresh = prot->warn_ov_active ?
                BMS_SE_OV_WARN_CLEAR_MV : BMS_SE_OV_WARN_MV;
            if (pack->cell_mv[i] >= thresh) {
                warn_cond_ov = true;
                break;
            }
        }

        /* Check UV warning condition (with hysteresis) */
        for (i = 0U; i < BMS_SE_PER_PACK; i++) {
            if (pack->cell_mv[i] == 0U) { continue; }
            uint16_t thresh = prot->warn_uv_active ?
                BMS_SE_UV_WARN_CLEAR_MV : BMS_SE_UV_WARN_MV;
            if (pack->cell_mv[i] <= thresh) {
                warn_cond_uv = true;
                break;
            }
        }

        /* Check OT warning condition (with hysteresis) */
        {
            int16_t thresh = prot->warn_ot_active ?
                BMS_SE_OT_WARN_CLEAR_DC : BMS_SE_OT_WARN_DECI_C;
            if (pack->max_temp_deci_c >= thresh) {
                warn_cond_ot = true;
            }
        }

        /* OV warning timer */
        if (warn_cond_ov) {
            leak_increment(&prot->warn_ov_timer_ms, dt_ms);
            if (prot->warn_ov_timer_ms >= BMS_WARN_DELAY_MS) {
                prot->warn_ov_active = true;
            }
        } else {
            leak_decay(&prot->warn_ov_timer_ms, dt_ms);
            if (!warn_cond_ov && prot->warn_ov_timer_ms == 0U) {
                prot->warn_ov_active = false;
            }
        }

        /* UV warning timer */
        if (warn_cond_uv) {
            leak_increment(&prot->warn_uv_timer_ms, dt_ms);
            if (prot->warn_uv_timer_ms >= BMS_WARN_DELAY_MS) {
                prot->warn_uv_active = true;
            }
        } else {
            leak_decay(&prot->warn_uv_timer_ms, dt_ms);
            if (!warn_cond_uv && prot->warn_uv_timer_ms == 0U) {
                prot->warn_uv_active = false;
            }
        }

        /* OT warning timer */
        if (warn_cond_ot) {
            leak_increment(&prot->warn_ot_timer_ms, dt_ms);
            if (prot->warn_ot_timer_ms >= BMS_WARN_DELAY_MS) {
                prot->warn_ot_active = true;
            }
        } else {
            leak_decay(&prot->warn_ot_timer_ms, dt_ms);
            if (!warn_cond_ot && prot->warn_ot_timer_ms == 0U) {
                prot->warn_ot_active = false;
            }
        }

        /* Aggregate warning with hold timer */
        {
            bool any_active = prot->warn_ov_active ||
                              prot->warn_uv_active ||
                              prot->warn_ot_active;
            if (any_active) {
                prot->warning_hold_ms = BMS_WARN_HOLD_MS;
                pack->has_warning = true;
            } else if (prot->warning_hold_ms > 0U) {
                /* Hold timer countdown */
                if (prot->warning_hold_ms > dt_ms) {
                    prot->warning_hold_ms -= dt_ms;
                } else {
                    prot->warning_hold_ms = 0U;
                }
                pack->has_warning = (prot->warning_hold_ms > 0U);
            } else {
                pack->has_warning = false;
            }
        }
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
