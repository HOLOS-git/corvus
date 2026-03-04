/**
 * @file bms_types.h
 * @brief All type definitions for Corvus Orca ESS BMS
 *
 * Street Smart Edition.
 * ALL voltages: uint16_t in millivolts
 * ALL currents: int32_t in milliamps
 * ALL temperatures: int16_t in 0.1°C (deci-Celsius)
 * ALL times: uint32_t in milliseconds
 * NO float, NO double, NO bare int.
 *
 * Reviewer findings addressed:
 *   P0-01: Sensor fault tracking per sensor (Dave, Priya)
 *   P0-02: Per-module I2C failure counter (Dave, Catherine)
 *   P0-04: dT/dt alarm flag (Catherine, Mikael, Henrik, Priya)
 *   P1-03..06: Safety I/O fault flags (Catherine, Henrik, Priya)
 *   P2-06: CAN input validation types (Yara)
 */

#ifndef BMS_TYPES_H
#define BMS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "bms_config.h"

/* ── Logging ───────────────────────────────────────────────────────── */
#ifdef DESKTOP_BUILD
  #include <stdio.h>
  #define BMS_LOG(...) fprintf(stderr, "[BMS] " __VA_ARGS__), fprintf(stderr, "\n")
#else
  #define BMS_LOG(fmt, ...) ((void)0)
#endif

/* ── Pack Operation Modes (§7.1, Table 15) ─────────────────────────── */
typedef enum {
    BMS_MODE_OFF        = 0,
    BMS_MODE_POWER_SAVE = 1,
    BMS_MODE_FAULT      = 2,
    BMS_MODE_READY      = 3,
    BMS_MODE_CONNECTING = 4,
    BMS_MODE_CONNECTED  = 5,
    BMS_MODE_NOT_READY  = 6,
    BMS_MODE_COUNT      = 7
} bms_pack_mode_t;

/* ── Contactor States ──────────────────────────────────────────────── */
typedef enum {
    CONTACTOR_OPEN       = 0,
    CONTACTOR_PRE_CHARGE = 1,
    CONTACTOR_CLOSING    = 2,
    CONTACTOR_CLOSED     = 3,
    CONTACTOR_OPENING    = 4,
    CONTACTOR_WELDED     = 5
} bms_contactor_state_t;

/* ── Fault Flags — P1-03..06: expanded with safety I/O ─────────────── */
typedef struct {
    uint32_t cell_ov        : 1;
    uint32_t cell_uv        : 1;
    uint32_t cell_ot        : 1;
    uint32_t hw_ov          : 1;
    uint32_t hw_uv          : 1;
    uint32_t hw_ot          : 1;
    uint32_t oc_charge      : 1;
    uint32_t oc_discharge   : 1;
    uint32_t sc_discharge   : 1;
    uint32_t contactor_weld : 1;
    uint32_t ems_timeout    : 1;
    uint32_t comm_loss      : 1;  /* P0-02: NOW latches fault */
    uint32_t imbalance      : 1;
    uint32_t sensor_fault   : 1;  /* P0-01: temperature sensor failure */
    uint32_t dtdt_alarm     : 1;  /* P0-04: thermal rate-of-rise */
    uint32_t subzero_charge : 1;  /* P0-05: charging below freezing */
    uint32_t gas_alarm_low  : 1;  /* P1-03: gas low alarm */
    uint32_t gas_alarm_high : 1;  /* P1-03: gas high alarm → shutdown */
    uint32_t vent_failure   : 1;  /* P1-04: ventilation failure */
    uint32_t fire_detected  : 1;  /* P1-05: fire detection */
    uint32_t fire_suppression:1;  /* P1-05: suppression activated */
    uint32_t imd_alarm      : 1;  /* P1-06: insulation monitoring alarm */
    uint32_t plausibility   : 1;  /* P2-07: I2C plausibility check fail */
    uint32_t iwdg_reset     : 1;  /* P1-02: previous reset was IWDG */
    uint32_t fan_failure    : 1;  /* P3-03: fan tach below threshold */
    uint32_t reserved       : 7;
} bms_fault_flags_t;

_Static_assert(sizeof(bms_fault_flags_t) == 4U, "Fault flags must be 32 bits");

/* ── BQ76952 Safety Status ─────────────────────────────────────────── */
typedef struct {
    uint8_t safety_alert_a;
    uint8_t safety_status_a;
    uint8_t safety_alert_b;
    uint8_t safety_status_b;
    uint8_t safety_alert_c;
} bms_bq_safety_t;

/* ── P0-01: Per-Sensor Fault Tracking ──────────────────────────────── */
typedef struct {
    uint8_t consec_fault_count;    /* consecutive bad reads */
    bool    faulted;               /* sensor declared dead */
    int16_t last_valid_deci_c;     /* last known good reading */
} bms_sensor_fault_t;

/* ── Per-Module Data ───────────────────────────────────────────────── */
typedef struct {
    uint16_t         cell_mv[BMS_SE_PER_MODULE];
    uint16_t         prev_cell_mv[BMS_SE_PER_MODULE]; /* P2-07: for dV/dt check */
    int16_t          temp_deci_c[BMS_TEMPS_PER_MODULE];
    uint16_t         stack_mv;
    bms_bq_safety_t  bq_safety;
    bool             comm_ok;
    uint8_t          i2c_fail_count;   /* P0-02: consecutive failure counter */
    bms_sensor_fault_t sensor_fault[BMS_TEMPS_PER_MODULE]; /* P0-01 */
} bms_module_data_t;

/* ── Pack-Level Aggregated Data ────────────────────────────────────── */
typedef struct {
    uint16_t          cell_mv[BMS_SE_PER_PACK];
    uint32_t          pack_voltage_mv;
    int32_t           pack_current_ma;
    uint16_t          max_cell_mv;
    uint16_t          min_cell_mv;
    uint16_t          avg_cell_mv;
    int16_t           max_temp_deci_c;
    int16_t           min_temp_deci_c;
    uint16_t          soc_hundredths;       /* 0–10000 */
    bms_module_data_t modules[BMS_NUM_MODULES];
    bms_fault_flags_t faults;
    bool              fault_latched;
    bool              has_warning;
    int32_t           charge_limit_ma;
    int32_t           discharge_limit_ma;
    bms_contactor_state_t contactor_state;
    bms_pack_mode_t   mode;
    uint32_t          uptime_ms;
    uint32_t          last_ems_msg_ms;
    uint32_t          bus_voltage_mv;       /* P0-03: actual bus voltage */
} bms_pack_data_t;

/* ── CAN Message IDs ───────────────────────────────────────────────── */
typedef enum {
    CAN_ID_ARRAY_STATUS    = 0x100U,
    CAN_ID_LIMITS          = 0x105U,
    CAN_ID_HEARTBEAT       = 0x108U,
    CAN_ID_PACK_STATUS     = 0x110U,
    CAN_ID_PACK_ALARMS     = 0x120U,
    CAN_ID_PACK_VOLTAGES   = 0x130U,
    CAN_ID_CELL_BROADCAST  = 0x131U,
    CAN_ID_PACK_TEMPS      = 0x140U,
    CAN_ID_SAFETY_IO       = 0x150U,  /* NEW: safety I/O status */
    CAN_ID_DTDT_ALARM      = 0x151U,  /* NEW: dT/dt alarm */
    CAN_ID_EMS_COMMAND     = 0x200U,
    CAN_ID_EMS_HEARTBEAT   = 0x210U
} bms_can_id_t;

/* ── CAN Frame ─────────────────────────────────────────────────────── */
typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} bms_can_frame_t;

/* ── EMS Command Types ─────────────────────────────────────────────── */
typedef enum {
    EMS_CMD_NONE           = 0,
    EMS_CMD_CONNECT_CHG    = 1,
    EMS_CMD_CONNECT_DCHG   = 2,
    EMS_CMD_DISCONNECT     = 3,
    EMS_CMD_RESET_FAULTS   = 4,
    EMS_CMD_POWER_SAVE     = 5,
    EMS_CMD_SET_LIMITS     = 6,
    EMS_CMD_COUNT          = 7
} bms_ems_cmd_type_t;

/* ── EMS Command ───────────────────────────────────────────────────── */
typedef struct {
    bms_ems_cmd_type_t type;
    int32_t            charge_limit_ma;
    int32_t            discharge_limit_ma;
    uint32_t           timestamp_ms;
    bool               valid;          /* P2-06: passed validation */
} bms_ems_command_t;

/* ── Safety I/O State (P1-03..06) ──────────────────────────────────── */
typedef enum {
    SAFETY_IO_NORMAL = 0,
    SAFETY_IO_WARNING = 1,
    SAFETY_IO_ALARM = 2,
    SAFETY_IO_SHUTDOWN = 3
} bms_safety_io_level_t;

typedef struct {
    bms_safety_io_level_t gas_level;
    bms_safety_io_level_t vent_level;
    bms_safety_io_level_t fire_level;
    bms_safety_io_level_t imd_level;
    bool                  fire_suppression_active;
    bool                  vent_running;
    uint32_t              vent_restore_timer_ms;
    /* P1-06: IMD resistance monitoring */
    uint32_t              imd_resistance_kohm;       /* last read insulation resistance */
    uint32_t              imd_log_timer_ms;           /* timer for periodic NVM logging */
    uint32_t              imd_last_logged_kohm;       /* P3-02: last logged value for delta check */
} bms_safety_io_state_t;

#endif /* BMS_TYPES_H */
