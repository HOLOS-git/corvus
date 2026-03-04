/**
 * @file bms_config.h
 * @brief Corvus Orca ESS — All configuration constants, thresholds, timing
 *
 * Street Smart Edition: Ground-up rewrite incorporating 6-reviewer panel findings.
 * All magic numbers are named constants here. No magic numbers elsewhere.
 *
 * Reviewer findings addressed:
 *   P0-01: Temperature sensor fault thresholds (Dave, Priya, Catherine, Yara)
 *   P0-02: I2C consecutive failure count (Dave, Catherine)
 *   P0-04: dT/dt thresholds and averaging window (Catherine, Mikael, Henrik, Priya)
 *   P0-05: Sub-zero charge current margin (Dave)
 *   P1-02: IWDG timeout (Henrik, Yara)
 *   P1-03: Gas alarm response timing (Catherine, Mikael, Henrik, Priya)
 *   P2-05: Fault reset limits (Yara)
 *   P2-06: CAN parameter range limits (Yara)
 *
 * Reference: Orca ESS Integrator Manual (1007768 Rev V), Table 13, §7.4
 */

#ifndef BMS_CONFIG_H
#define BMS_CONFIG_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════
 * Pack Topology
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_NUM_MODULES             22U
#define BMS_SE_PER_MODULE           14U
#define BMS_SE_PER_PACK             (BMS_NUM_MODULES * BMS_SE_PER_MODULE) /* 308 */
#define BMS_CELLS_PER_BQ76952       16U
#define BMS_NUM_BQ76952             22U
#define BMS_MAX_PACKS               16U
#define BMS_NOMINAL_CAPACITY_MAH    128000

/* ═══════════════════════════════════════════════════════════════════════
 * Temperature Sensors
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_TEMPS_PER_MODULE        3U
#define BMS_TOTAL_TEMP_SENSORS      (BMS_NUM_MODULES * BMS_TEMPS_PER_MODULE) /* 66 */

/* ═══════════════════════════════════════════════════════════════════════
 * SE Alarm Thresholds (Table 13) — mV or deci-°C
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_SE_OV_FAULT_MV          4225U   /* 4.225V, 5s delay */
#define BMS_SE_UV_FAULT_MV          3000U   /* 3.000V, 5s delay */
#define BMS_SE_OT_FAULT_DECI_C      650    /* 65.0°C, 5s delay */

#define BMS_SE_OV_WARN_MV           4210U
#define BMS_SE_UV_WARN_MV           3200U
#define BMS_SE_OT_WARN_DECI_C        600

#define BMS_SE_OV_WARN_CLEAR_MV     4190U   /* 20mV hysteresis */
#define BMS_SE_UV_WARN_CLEAR_MV     3220U
#define BMS_SE_OT_WARN_CLEAR_DC      570    /* 3°C hysteresis */

/* ═══════════════════════════════════════════════════════════════════════
 * Hardware Safety Thresholds (BQ76952 Autonomous — P1-01)
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_HW_OV_MV               4300U   /* 4.300V, 1s */
#define BMS_HW_UV_MV               2700U   /* 2.700V, 1s */
#define BMS_HW_OT_DECI_C            700    /* 70.0°C, 5s */
#define BMS_HW_SC_THRESHOLD_MA    500000   /* 500A short circuit */

#define BMS_HW_OV_DELAY_MS         1000U
#define BMS_HW_UV_DELAY_MS         1000U
#define BMS_HW_OT_DELAY_MS         5000U

/* ═══════════════════════════════════════════════════════════════════════
 * Fault Timer Delays
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_SE_FAULT_DELAY_MS       5000U
#define BMS_LEAK_DECAY_SHIFT           1U   /* decay = dt >> 1 = dt/2 */

/* ═══════════════════════════════════════════════════════════════════════
 * P0-01: Temperature Sensor Fault Detection (Dave, Priya, Catherine, Yara)
 * Sentinel value for failed reads, plausibility bounds, consecutive scan count
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_TEMP_SENSOR_SENTINEL    INT16_MIN  /* -32768: impossible temperature */
#define BMS_TEMP_PLAUSIBLE_MIN_DC   (-400)     /* -40.0°C minimum plausible */
#define BMS_TEMP_PLAUSIBLE_MAX_DC    1200      /* 120.0°C maximum plausible */
#define BMS_TEMP_ZERO_EXACT_DC         0       /* 0.0°C — suspicious if exact */
#define BMS_TEMP_FAULT_CONSEC_SCANS    3U      /* 3 consecutive bad → fault */
#define BMS_TEMP_ADJACENT_DELTA_DC   200       /* 20°C max inter-sensor delta */

/* ═══════════════════════════════════════════════════════════════════════
 * P0-02: I2C Communication Fault Latching (Dave, Catherine)
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_I2C_FAULT_CONSEC_COUNT     3U      /* 3 consecutive → latch */
#define BMS_I2C_RECOVERY_ATTEMPTS      2U      /* bus recovery tries */

/* ═══════════════════════════════════════════════════════════════════════
 * P0-04: dT/dt Thermal Rate-of-Rise (Catherine, Mikael, Henrik, Priya)
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_DTDT_ALARM_DECI_C_PER_MIN  10     /* 1.0°C/min threshold */
#define BMS_DTDT_WINDOW_SAMPLES        30U    /* 30 samples for moving avg */
#define BMS_DTDT_SUSTAIN_MS          30000U   /* 30s sustained → alarm */
#define BMS_DTDT_SAMPLE_PERIOD_MS     1000U   /* 1 sample/sec */

/* ═══════════════════════════════════════════════════════════════════════
 * P0-05: Sub-Zero Charging (Dave)
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_SUBZERO_CHARGE_MARGIN_MA      0   /* 0A margin below freezing */
#define BMS_SUBZERO_CHARGE_FAULT_MS    5000U  /* 5s of sub-zero charging → fault */
#define BMS_SUBZERO_TEMP_THRESHOLD_DC     0   /* 0.0°C = freezing */

/* ═══════════════════════════════════════════════════════════════════════
 * Cell Imbalance
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_IMBALANCE_WARN_MV          50U
#define BMS_BALANCE_THRESHOLD_MV       20U

/* ═══════════════════════════════════════════════════════════════════════
 * Contactor Timing — P0-03: Must use bus voltage (Dave)
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_PRECHARGE_TIMEOUT_MS     5000U
#define BMS_CONTACTOR_CLOSE_MS        100U
#define BMS_CONTACTOR_OPEN_MS         100U
#define BMS_WELD_DETECT_MS            500U    /* Extended from 200ms per Catherine/Dave */
#define BMS_PRECHARGE_VOLT_PCT         95U    /* % of BUS voltage (not pack!) */
#define BMS_VOLTAGE_MATCH_MV        (1200U * BMS_NUM_MODULES) /* 26.4V */

/* ═══════════════════════════════════════════════════════════════════════
 * P3-04: ADC Bus Voltage Scaling (Dave — must be consistent everywhere)
 * 12-bit ADC (0-4095) through voltage divider. Single calibration point.
 * bus_mv = adc_raw * SCALE_NUM / SCALE_DEN
 * Example: 1000V full-scale with 1:250 divider → 4V at ADC → ~4095 counts
 *          bus_mv = raw * 1000000 / 4095 ≈ raw * 244
 * Adjust NUM/DEN for actual hardware voltage divider ratio.
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_ADC_BUS_VOLTAGE_SCALE_NUM  1000000U  /* numerator: full-scale mV */
#define BMS_ADC_BUS_VOLTAGE_SCALE_DEN     4095U  /* denominator: ADC max count */

/* ═══════════════════════════════════════════════════════════════════════
 * CAN Communication — P2-06: Input validation (Yara)
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_CAN_HEARTBEAT_MS         1000U
#define BMS_EMS_WATCHDOG_MS          5000U
#define BMS_EMS_READY_TIMEOUT_MS  1800000U    /* 30 min in READY → POWER_SAVE (P2-09) */

/* ═══════════════════════════════════════════════════════════════════════
 * CC-01 / P2-01: CAN Authentication (stub — full CMAC in Phase 2)
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_CAN_AUTH_ENABLED           0U     /* 0=disabled (stub), 1=enforce auth */
#define BMS_CAN_SEQ_COUNTER_MAX   0xFFFFU     /* 16-bit sequence counter wrap */

/* ═══════════════════════════════════════════════════════════════════════
 * P2-07: I2C Plausibility (Dave, Yara, Priya)
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_STACK_VS_CELLS_PCT         2U     /* 2% divergence → anomaly */
#define BMS_CELL_DV_DT_MAX_MV        50U     /* 50mV per 10ms max rate */
#define BMS_INTER_MODULE_TEMP_DELTA_DC 200    /* 20°C inter-module max */

/* ═══════════════════════════════════════════════════════════════════════
 * Task Periods
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_MONITOR_PERIOD_MS          10U
#define BMS_PROTECTION_PERIOD_MS       10U
#define BMS_CAN_TX_PERIOD_MS          100U
#define BMS_CONTACTOR_PERIOD_MS        50U
#define BMS_STATE_PERIOD_MS           100U
#define BMS_THERMAL_PERIOD_MS        1000U    /* dT/dt at 1Hz */
#define BMS_SAFETY_IO_PERIOD_MS       100U

/* ═══════════════════════════════════════════════════════════════════════
 * P1-02: Hardware Watchdog (Henrik, Yara)
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_IWDG_TIMEOUT_MS           100U    /* ≤100ms per Henrik */
#define BMS_IWDG_PRESCALER              4U    /* LSI/4 */

/* ═══════════════════════════════════════════════════════════════════════
 * P1-03/04/05/06: Safety I/O Timing
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_GAS_SHUTDOWN_MS          2000U    /* 2s max to shutdown */
#define BMS_VENT_RESTORE_DELAY_MS   30000U    /* 30s delay after vent restore */
#define BMS_FIRE_INTERLOCK_MANUAL     true    /* Manual-only reset after fire */

/* P1-06: IMD insulation resistance monitoring (IEC 61557-8) */
#define BMS_IMD_ALARM_THRESHOLD_KOHM  100U    /* 100 kΩ/V per IEC 61557-8 for 1kV system */
#define BMS_IMD_WARNING_THRESHOLD_KOHM 200U  /* P3-02: rapid logging below this (Catherine) */
#define BMS_IMD_LOG_INTERVAL_RAPID_MS  60000U  /* 60s when below warning threshold */
#define BMS_IMD_LOG_INTERVAL_SLOW_MS 3600000U  /* 1 hour for normal trend data */
#define BMS_IMD_ADC_SCALE_KOHM_PER_BIT  1U  /* ADC-to-kΩ scaling (hardware-dependent) */

/* ═══════════════════════════════════════════════════════════════════════
 * P3-03: Fan Tachometer / Cooling Failure Detection (Priya)
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_FAN_MIN_RPM               500U    /* Below this while cooling ON → alarm */
#define BMS_FAN_FAIL_CONSEC_COUNT       3U    /* Consecutive low-RPM reads before alarm */
#define BMS_FAN_DTDT_COMPENSATE_DECI_C  5     /* Lower dT/dt threshold on fan failure (0.5°C/min) */

/* ═══════════════════════════════════════════════════════════════════════
 * Fault Reset — P2-05: Timer preservation (Yara)
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_FAULT_RESET_HOLD_MS     60000U
#define BMS_MAX_RESETS_PER_HOUR        3U
#define BMS_CONSEC_FAULT_MANUAL_LIMIT  5U     /* N same-type → manual only */

/* ═══════════════════════════════════════════════════════════════════════
 * Current Limits
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_MAX_CHARGE_MA           384000    /* 3C × 128Ah */
#define BMS_MAX_DISCHARGE_MA        640000    /* 5C × 128Ah */
#define BMS_COULOMBIC_EFFICIENCY_PPT   998U   /* 0.998 */

/* ═══════════════════════════════════════════════════════════════════════
 * NVM Configuration
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_NVM_FAULT_LOG_SIZE         64U

/* ═══════════════════════════════════════════════════════════════════════
 * Warning Timing
 * ═══════════════════════════════════════════════════════════════════════ */
#define BMS_WARN_DELAY_MS            5000U
#define BMS_WARN_HOLD_MS            10000U

/* ═══════════════════════════════════════════════════════════════════════
 * Compile-Time Validation
 * ═══════════════════════════════════════════════════════════════════════ */
_Static_assert(BMS_SE_PER_PACK == 308U, "Pack must have 308 series elements");
_Static_assert(BMS_TOTAL_TEMP_SENSORS == 66U, "Must have 66 temperature sensors");
_Static_assert(BMS_IWDG_TIMEOUT_MS <= 100U, "P1-02: IWDG must be ≤100ms");
_Static_assert(BMS_SUBZERO_CHARGE_MARGIN_MA == 0, "P0-05: 0A margin below freezing");
_Static_assert(BMS_I2C_FAULT_CONSEC_COUNT >= 3U, "P0-02: Need ≥3 consecutive failures");

#endif /* BMS_CONFIG_H */
