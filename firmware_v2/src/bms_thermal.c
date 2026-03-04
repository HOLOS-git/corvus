/**
 * @file bms_thermal.c
 * @brief dT/dt thermal rate-of-rise detection
 *
 * Street Smart Edition. NEW — not in original firmware.
 * P0-04: dT/dt detection (Catherine, Mikael, Henrik, Priya)
 *
 * "dT/dt detection is the single most cost-effective safety improvement.
 *  ~50-80 lines of C code. No hardware changes." — Priya
 *
 * "dT/dt > 1°C/min with no corresponding load change is the single best
 *  early indicator of thermal runaway, providing 5-15 minutes additional
 *  warning." — Implementation Plan
 *
 * Algorithm:
 *   1. Store temperature history in circular buffer (1 sample/sec, 30 samples)
 *   2. Compute dT/dt = (T_now - T_oldest) / window_seconds → deci-°C/min
 *   3. If dT/dt > 1°C/min (10 deci-°C/min) sustained 30s AND current
 *      hasn't increased proportionally → alarm
 *   4. dT/dt alarm → fault_latched, distinct CAN message
 */

#include "bms_thermal.h"
#include "bms_hal.h"
#include "bms_config.h"
#include <string.h>

/* Forward declarations */
static void leak_timer_inc(uint32_t *timer, uint32_t dt_ms);
static void leak_timer_dec(uint32_t *timer, uint32_t dt_ms);

void bms_thermal_init(bms_thermal_state_t *therm)
{
    memset(therm, 0, sizeof(*therm));
    therm->alarm_active = false;
    therm->history_idx = 0U;
    therm->history_count = 0U;
}

void bms_thermal_run(bms_thermal_state_t *therm,
                     bms_pack_data_t *pack,
                     uint32_t dt_ms)
{
    uint8_t mod, sens;
    uint16_t sensor_idx = 0U;

    /* Store current temperatures in history buffer */
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
            /* P0-01: Skip faulted sensors */
            if (pack->modules[mod].sensor_fault[sens].faulted) {
                sensor_idx++;
                continue;
            }
            therm->temp_history[sensor_idx][therm->history_idx] =
                pack->modules[mod].temp_deci_c[sens];
            sensor_idx++;
        }
    }

    therm->history_idx = (therm->history_idx + 1U) % BMS_DTDT_WINDOW_SAMPLES;
    if (therm->history_count < BMS_DTDT_WINDOW_SAMPLES) {
        therm->history_count++;
    }

    /* Need full window before computing dT/dt */
    if (therm->history_count < BMS_DTDT_WINDOW_SAMPLES) {
        return;
    }

    /* Compute dT/dt for each sensor */
    uint8_t oldest_idx = therm->history_idx; /* oldest = current position (circular) */
    bool any_alarm = false;

    for (sensor_idx = 0U; sensor_idx < BMS_TOTAL_TEMP_SENSORS; sensor_idx++) {
        int16_t t_now = therm->temp_history[sensor_idx]
            [(therm->history_idx + BMS_DTDT_WINDOW_SAMPLES - 1U) % BMS_DTDT_WINDOW_SAMPLES];
        int16_t t_old = therm->temp_history[sensor_idx][oldest_idx];

        /* dT/dt in deci-°C per 30 seconds.
         * Convert to deci-°C per minute: multiply by 2 (30s → 60s) */
        int16_t dtdt = (int16_t)((int32_t)(t_now - t_old) * 2);
        therm->dtdt_deci_c_per_min[sensor_idx] = dtdt;

        /* Check alarm threshold */
        if (dtdt > BMS_DTDT_ALARM_DECI_C_PER_MIN) {
            /* Check if current increased proportionally (load-correlated heating is OK) */
            int32_t abs_current = pack->pack_current_ma;
            if (abs_current < 0) { abs_current = -abs_current; }

            /* If current hasn't increased significantly, this is anomalous */
            int32_t current_increase = abs_current - therm->baseline_current_ma;
            if (current_increase < 0) { current_increase = 0; }

            /* Heuristic: >50A increase explains thermal rise from I²R */
            if (current_increase < 50000) {
                leak_timer_inc(&therm->dtdt_alarm_timer_ms[sensor_idx], dt_ms);
                if (therm->dtdt_alarm_timer_ms[sensor_idx] >= BMS_DTDT_SUSTAIN_MS) {
                    any_alarm = true;
                    therm->alarm_sensor_idx = (uint8_t)sensor_idx;
                }
            } else {
                leak_timer_dec(&therm->dtdt_alarm_timer_ms[sensor_idx], dt_ms);
            }
        } else {
            leak_timer_dec(&therm->dtdt_alarm_timer_ms[sensor_idx], dt_ms);
        }
    }

    /* Update baseline current periodically */
    {
        int32_t abs_current = pack->pack_current_ma;
        if (abs_current < 0) { abs_current = -abs_current; }
        therm->baseline_current_ma = abs_current;
    }

    /* ── P3-03: Fan tachometer / cooling failure detection (Priya) ── */
    {
        uint16_t fan_rpm = hal_fan_tach_read_rpm();
        /* Detect failure: RPM below threshold while cooling is commanded ON */
        if (therm->cooling_commanded && fan_rpm < BMS_FAN_MIN_RPM) {
            if (therm->fan_fail_consec < 255U) {
                therm->fan_fail_consec++;
            }
            if (therm->fan_fail_consec >= BMS_FAN_FAIL_CONSEC_COUNT && !therm->fan_failure) {
                therm->fan_failure = true;
                pack->faults.fan_failure = 1U;
                pack->has_warning = true;
                BMS_LOG("P3-03: Fan failure detected (RPM=%u, threshold=%u)",
                        fan_rpm, BMS_FAN_MIN_RPM);
            }
        } else {
            therm->fan_fail_consec = 0U;
            if (therm->fan_failure) {
                therm->fan_failure = false;
                pack->faults.fan_failure = 0U;
                BMS_LOG("P3-03: Fan recovered (RPM=%u)", fan_rpm);
            }
        }
    }

    /* P3-03: On fan failure, lower dT/dt alarm threshold as compensating measure */
    {
        int16_t effective_threshold = BMS_DTDT_ALARM_DECI_C_PER_MIN;
        if (therm->fan_failure) {
            effective_threshold = BMS_FAN_DTDT_COMPENSATE_DECI_C;
        }
        /* Re-check alarm sensors against possibly-lowered threshold */
        for (sensor_idx = 0U; sensor_idx < BMS_TOTAL_TEMP_SENSORS; sensor_idx++) {
            if (therm->dtdt_deci_c_per_min[sensor_idx] > effective_threshold &&
                therm->dtdt_deci_c_per_min[sensor_idx] <= BMS_DTDT_ALARM_DECI_C_PER_MIN) {
                /* This sensor is between the lowered and normal threshold —
                 * only alarming because of fan failure compensation */
                int32_t abs_current = pack->pack_current_ma;
                if (abs_current < 0) { abs_current = -abs_current; }
                int32_t current_increase = abs_current - therm->baseline_current_ma;
                if (current_increase < 0) { current_increase = 0; }
                if (current_increase < 50000) {
                    leak_timer_inc(&therm->dtdt_alarm_timer_ms[sensor_idx], dt_ms);
                    if (therm->dtdt_alarm_timer_ms[sensor_idx] >= BMS_DTDT_SUSTAIN_MS) {
                        any_alarm = true;
                        therm->alarm_sensor_idx = (uint8_t)sensor_idx;
                    }
                }
            }
        }
    }

    if (any_alarm && !therm->alarm_active) {
        therm->alarm_active = true;
        pack->faults.dtdt_alarm = 1U;
        pack->fault_latched = true;
        BMS_LOG("P0-04: dT/dt alarm! sensor %u, rate=%d deci-C/min",
                therm->alarm_sensor_idx,
                therm->dtdt_deci_c_per_min[therm->alarm_sensor_idx]);
    }
}

/* Internal timer helpers (avoid dependency on protection module) */
static void leak_timer_inc(uint32_t *timer, uint32_t dt_ms)
{
    if (*timer <= (0xFFFFFFFFU - dt_ms)) {
        *timer += dt_ms;
    }
}

static void leak_timer_dec(uint32_t *timer, uint32_t dt_ms)
{
    uint32_t decay = dt_ms / 2U;
    if (*timer > decay) {
        *timer -= decay;
    } else {
        *timer = 0U;
    }
}

int16_t bms_thermal_get_dtdt(const bms_thermal_state_t *therm,
                              uint8_t sensor_idx)
{
    if (sensor_idx >= BMS_TOTAL_TEMP_SENSORS) { return 0; }
    return therm->dtdt_deci_c_per_min[sensor_idx];
}

bool bms_thermal_alarm_active(const bms_thermal_state_t *therm)
{
    return therm->alarm_active;
}
