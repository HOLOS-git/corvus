/**
 * bms_config.h — Corvus Orca ESS configuration parameters
 *
 * All thresholds from Corvus Orca ESS Integrator Manual (1007768 Rev V),
 * Table 13: Alarm Threshold Values, Section 7.4: Current Limits.
 *
 * SIMULATION DISCLAIMER: This is a firmware architecture demo for the
 * Corvus Orca ESS. Not certified production code. Not affiliated with
 * or endorsed by Corvus Energy.
 */

#ifndef BMS_CONFIG_H
#define BMS_CONFIG_H

/* ── Pack topology ─────────────────────────────────────────────────── */
#define BMS_NUM_MODULES            22U    /* modules per pack              */
#define BMS_SE_PER_MODULE          14U    /* series elements per module    */
#define BMS_SE_PER_PACK            (BMS_NUM_MODULES * BMS_SE_PER_MODULE) /* 308 */
#define BMS_CELLS_PER_BQ76952      16U    /* max cells per BQ76952 ASIC   */
#define BMS_NUM_BQ76952            22U    /* one BQ76952 per module        */
#define BMS_MAX_PACKS              16U    /* max packs per array (§7)      */
#define BMS_NOMINAL_CAPACITY_MAH   128000 /* 128 Ah in milliamps          */

/* ── Thermistor channels per module ────────────────────────────────── */
#define BMS_TEMPS_PER_MODULE        3U    /* TS1, TS2, TS3 on BQ76952    */
#define BMS_TOTAL_TEMP_SENSORS     (BMS_NUM_MODULES * BMS_TEMPS_PER_MODULE) /* 66 */

/* ── SE alarm thresholds (Table 13) — all in mV or 0.1°C ──────────── */
#define BMS_SE_OV_FAULT_MV         4225U  /* 4.225V, 5s delay             */
#define BMS_SE_UV_FAULT_MV         3000U  /* 3.000V, 5s delay             */
#define BMS_SE_OT_FAULT_DECI_C      650   /* 65.0°C, 5s delay (signed)    */

#define BMS_SE_OV_WARN_MV          4210U  /* 4.210V, 5s delay             */
#define BMS_SE_UV_WARN_MV          3200U  /* 3.200V, 5s delay             */
#define BMS_SE_OT_WARN_DECI_C       600   /* 60.0°C, 5s delay (signed)    */

/* Warning clear thresholds (hysteresis deadband) */
#define BMS_SE_OV_WARN_CLEAR_MV    4190U  /* 20mV deadband                */
#define BMS_SE_UV_WARN_CLEAR_MV    3220U  /* 20mV deadband                */
#define BMS_SE_OT_WARN_CLEAR_DC     570   /* 3°C deadband (signed)        */

/* ── Hardware safety thresholds (Table 13) ─────────────────────────── */
#define BMS_HW_OV_MV              4300U   /* 4.300V, 1s                   */
#define BMS_HW_UV_MV              2700U   /* 2.700V, 1s                   */
#define BMS_HW_OT_DECI_C          700     /* 70.0°C, 5s (signed)          */

/* ── Fault timer delays in ms ──────────────────────────────────────── */
#define BMS_SE_FAULT_DELAY_MS      5000U  /* 5s for SE OV/UV/OT           */
#define BMS_HW_OV_DELAY_MS        1000U   /* 1s for HW OV/UV              */
#define BMS_HW_UV_DELAY_MS        1000U
#define BMS_HW_OT_DELAY_MS        5000U   /* 5s for HW OT                 */

/* ── Leaky integrator decay: decay by dt/2 when condition clears ───── */
#define BMS_LEAK_DECAY_SHIFT        1U    /* right-shift by 1 = divide by 2 */

/* ── Cell imbalance threshold (mV) ─────────────────────────────────── */
#define BMS_IMBALANCE_WARN_MV       50U   /* max-min > 50mV → warning     */

/* ── Contactor timing (ms) ─────────────────────────────────────────── */
#define BMS_PRECHARGE_TIMEOUT_MS   5000U  /* Table 16: 5s pre-charge       */
#define BMS_CONTACTOR_CLOSE_MS      100U  /* max close verification time   */
#define BMS_CONTACTOR_OPEN_MS       100U  /* max open verification time    */
#define BMS_WELD_DETECT_MS          200U  /* post-open weld check window   */
#define BMS_PRECHARGE_VOLT_PCT       95U  /* % of bus voltage for complete */

/* ── Voltage match for connection (§7.2) ───────────────────────────── */
/* 1.2V per module × 22 modules = 26.4V → 26400 mV */
#define BMS_VOLTAGE_MATCH_MV       (1200U * BMS_NUM_MODULES)

/* ── CAN communication ─────────────────────────────────────────────── */
#define BMS_CAN_HEARTBEAT_MS       1000U  /* 1s heartbeat interval         */
#define BMS_EMS_WATCHDOG_MS        5000U  /* 5s EMS timeout → safe state   */

/* ── Task periods (ms) ─────────────────────────────────────────────── */
#define BMS_MONITOR_PERIOD_MS        10U
#define BMS_PROTECTION_PERIOD_MS     10U
#define BMS_CAN_TX_PERIOD_MS        100U
#define BMS_CONTACTOR_PERIOD_MS      50U
#define BMS_STATE_PERIOD_MS         100U

/* ── Fault reset safe-state hold time (§6.3.5) ────────────────────── */
#define BMS_FAULT_RESET_HOLD_MS   60000U  /* 60 seconds                    */

/* ── Current limits — fixed-point C-rate × 100 (centi-C) ──────────── */
/* Max charge: 3C × 128Ah = 384A = 384000 mA                             */
/* Max discharge: 5C × 128Ah = 640A = 640000 mA                          */
#define BMS_MAX_CHARGE_MA         384000
#define BMS_MAX_DISCHARGE_MA      640000

/* ── Coulombic efficiency (parts per thousand) ─────────────────────── */
#define BMS_COULOMBIC_EFFICIENCY_PPT  998U  /* 0.998 — charge only      */

#endif /* BMS_CONFIG_H */
