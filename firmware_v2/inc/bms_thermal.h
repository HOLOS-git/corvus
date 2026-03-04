/**
 * @file bms_thermal.h
 * @brief dT/dt monitoring and thermal management
 *
 * Street Smart Edition. NEW file — not in original firmware.
 * Reviewer findings addressed:
 *   P0-04: dT/dt detection (Catherine, Mikael, Henrik, Priya)
 *     "dT/dt > 1°C/min with no corresponding load change is the single
 *      best early indicator of thermal runaway" — Priya
 *     "~50-80 lines of C code. No hardware changes." — Priya
 *
 * Implementation: Per-sensor 30-sample moving average of dT/dt.
 * Alarm when dT/dt > 1°C/min sustained 30s with no load increase.
 */

#ifndef BMS_THERMAL_H
#define BMS_THERMAL_H

#include "bms_types.h"

typedef struct {
    /* Per-sensor temperature history for dT/dt (circular buffer) */
    int16_t  temp_history[BMS_TOTAL_TEMP_SENSORS][BMS_DTDT_WINDOW_SAMPLES];
    uint8_t  history_idx;
    uint8_t  history_count;

    /* Per-sensor dT/dt in deci-°C per minute (filtered) */
    int16_t  dtdt_deci_c_per_min[BMS_TOTAL_TEMP_SENSORS];

    /* Per-sensor sustained alarm timer */
    uint32_t dtdt_alarm_timer_ms[BMS_TOTAL_TEMP_SENSORS];

    /* Current at time of dT/dt rise for load-correlation check */
    int32_t  baseline_current_ma;

    /* P3-03: Fan tachometer / cooling failure detection (Priya) */
    uint8_t  fan_fail_consec;          /* consecutive low-RPM reads */
    bool     fan_failure;              /* fan declared failed */
    bool     cooling_commanded;        /* true when cooling output is active */

    /* Global alarm state */
    bool     alarm_active;
    uint8_t  alarm_sensor_idx;  /* which sensor triggered */
} bms_thermal_state_t;

/**
 * Initialize thermal monitoring.
 */
void bms_thermal_init(bms_thermal_state_t *therm);

/**
 * Run one thermal monitoring cycle. Call at BMS_THERMAL_PERIOD_MS (1Hz).
 * Computes dT/dt for all sensors, checks alarm conditions.
 *
 * @param therm  thermal state
 * @param pack   pack data (reads temps, current)
 * @param dt_ms  time since last call
 *
 * Safety rationale: dT/dt provides 5-15 minutes additional warning
 * before absolute temperature thresholds are reached during thermal
 * runaway. This is the most cost-effective safety improvement identified
 * by the review panel.
 */
void bms_thermal_run(bms_thermal_state_t *therm,
                     bms_pack_data_t *pack,
                     uint32_t dt_ms);

/**
 * Get dT/dt for a specific sensor.
 * @return dT/dt in deci-°C per minute
 */
int16_t bms_thermal_get_dtdt(const bms_thermal_state_t *therm,
                              uint8_t sensor_idx);

/**
 * Check if dT/dt alarm is active.
 */
bool bms_thermal_alarm_active(const bms_thermal_state_t *therm);

#endif /* BMS_THERMAL_H */
