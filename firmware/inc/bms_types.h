/**
 * bms_types.h — Fixed-width types and core data structures
 *
 * ALL voltages: uint16_t in millivolts
 * ALL currents: int32_t in milliamps
 * ALL temperatures: int16_t in 0.1°C
 * ALL times: uint32_t in milliseconds
 * NO float, NO double, NO int.
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_TYPES_H
#define BMS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "bms_config.h"

/* ── Logging macro — compiles out on STM32 ─────────────────────────── */
#ifdef DESKTOP_BUILD
  #include <stdio.h>
  #define BMS_LOG(...) fprintf(stderr, "[BMS] " __VA_ARGS__), fprintf(stderr, "\n")
#else
  #define BMS_LOG(fmt, ...) ((void)0)
#endif

/* ── Pack operation modes — §7.1, Table 15 ─────────────────────────── */
typedef enum {
    BMS_MODE_OFF        = 0,   /* §7.1.7 */
    BMS_MODE_POWER_SAVE = 1,   /* §7.1.2 */
    BMS_MODE_FAULT      = 2,   /* §7.1.3 */
    BMS_MODE_READY      = 3,   /* §7.1.1 */
    BMS_MODE_CONNECTING = 4,   /* §7.1.4 */
    BMS_MODE_CONNECTED  = 5,   /* §7.1.5 */
    BMS_MODE_NOT_READY  = 6    /* §7.1.6 */
} bms_pack_mode_t;

/* ── Contactor states ──────────────────────────────────────────────── */
typedef enum {
    CONTACTOR_OPEN       = 0,
    CONTACTOR_PRE_CHARGE = 1,
    CONTACTOR_CLOSING    = 2,
    CONTACTOR_CLOSED     = 3,
    CONTACTOR_OPENING    = 4,
    CONTACTOR_WELDED     = 5
} bms_contactor_state_t;

/* ── Fault flags bitfield ──────────────────────────────────────────── */
typedef struct {
    uint32_t cell_ov       : 1;  /* per-cell overvoltage tripped       */
    uint32_t cell_uv       : 1;  /* per-cell undervoltage tripped      */
    uint32_t cell_ot       : 1;  /* over-temperature tripped           */
    uint32_t hw_ov         : 1;  /* hardware safety OV                 */
    uint32_t hw_uv         : 1;  /* hardware safety UV                 */
    uint32_t hw_ot         : 1;  /* hardware safety OT                 */
    uint32_t oc_charge     : 1;  /* overcurrent charge                 */
    uint32_t oc_discharge  : 1;  /* overcurrent discharge              */
    uint32_t sc_discharge  : 1;  /* short circuit discharge            */
    uint32_t contactor_weld: 1;  /* contactor welding detected         */
    uint32_t ems_timeout   : 1;  /* EMS watchdog expired               */
    uint32_t comm_loss     : 1;  /* BQ76952 communication failure      */
    uint32_t imbalance     : 1;  /* cell imbalance warning             */
    uint32_t reserved      : 19;
} bms_fault_flags_t;

/* ── BQ76952 safety status (from registers 0x02–0x06) ─────────────── */
typedef struct {
    uint8_t safety_alert_a;   /* reg 0x02 — SC, OC, OV, UV alerts    */
    uint8_t safety_status_a;  /* reg 0x03 — SC, OC, OV, UV status    */
    uint8_t safety_alert_b;   /* reg 0x04 — temperature alerts        */
    uint8_t safety_status_b;  /* reg 0x05 — temperature status        */
    uint8_t safety_alert_c;   /* reg 0x06 — OCD3, SCDL, OCDL, COVL   */
} bms_bq_safety_t;

/* ── Per-module data ───────────────────────────────────────────────── */
typedef struct {
    uint16_t        cell_mv[BMS_SE_PER_MODULE];  /* per-cell voltage mV     */
    int16_t         temp_deci_c[BMS_TEMPS_PER_MODULE]; /* 0.1°C per sensor  */
    uint16_t        stack_mv;                    /* module stack voltage mV  */
    bms_bq_safety_t bq_safety;                   /* raw BQ76952 safety regs */
    bool            comm_ok;                     /* I2C communication OK     */
} bms_module_data_t;

/* ── Pack-level aggregated data ────────────────────────────────────── */
typedef struct {
    /* Per-cell voltages — all 308 SE */
    uint16_t        cell_mv[BMS_SE_PER_PACK];

    /* Aggregated voltage/current */
    uint32_t        pack_voltage_mv;     /* sum of all cells              */
    int32_t         pack_current_ma;     /* measured pack current         */
    uint16_t        max_cell_mv;
    uint16_t        min_cell_mv;
    uint16_t        avg_cell_mv;

    /* Aggregated temperature */
    int16_t         max_temp_deci_c;
    int16_t         min_temp_deci_c;

    /* SoC — fixed-point 0.01% (0–10000 = 0.00%–100.00%) */
    uint16_t        soc_hundredths;

    /* Per-module data */
    bms_module_data_t modules[BMS_NUM_MODULES];

    /* Fault state */
    bms_fault_flags_t faults;
    bool              fault_latched;
    bool              has_warning;

    /* Current limits (mA) */
    int32_t         charge_limit_ma;
    int32_t         discharge_limit_ma;

    /* Contactor state */
    bms_contactor_state_t contactor_state;

    /* Pack mode */
    bms_pack_mode_t mode;

    /* Timestamps */
    uint32_t        uptime_ms;
    uint32_t        last_ems_msg_ms;     /* for EMS watchdog              */
} bms_pack_data_t;

/* ── CAN message IDs — mapped from Orca Modbus register groups ────── */
/* Base CAN ID 0x100, offset by Modbus register group address            */
typedef enum {
    CAN_ID_ARRAY_STATUS    = 0x100,  /* Modbus reg 0–25: array data     */
    CAN_ID_PACK_STATUS     = 0x110,  /* Modbus reg 50–97: pack status   */
    CAN_ID_PACK_ALARMS     = 0x120,  /* Modbus reg 400+: alarms         */
    CAN_ID_PACK_VOLTAGES   = 0x130,  /* cell voltage broadcast          */
    CAN_ID_PACK_TEMPS      = 0x140,  /* temperature broadcast           */
    CAN_ID_EMS_COMMAND     = 0x200,  /* Modbus reg 300–343: commands     */
    CAN_ID_EMS_HEARTBEAT   = 0x210   /* EMS heartbeat                   */
} bms_can_id_t;

/* ── CAN frame (CAN 2.0B standard) ────────────────────────────────── */
typedef struct {
    uint32_t id;           /* 11-bit standard ID                         */
    uint8_t  dlc;          /* data length 0–8                            */
    uint8_t  data[8];      /* payload                                    */
} bms_can_frame_t;

/* ── EMS command structure (decoded from CAN) ──────────────────────── */
typedef enum {
    EMS_CMD_NONE           = 0,
    EMS_CMD_CONNECT_CHG    = 1,  /* Connect all for charge              */
    EMS_CMD_CONNECT_DCHG   = 2,  /* Connect all for discharge           */
    EMS_CMD_DISCONNECT     = 3,  /* Disconnect all                      */
    EMS_CMD_RESET_FAULTS   = 4,  /* Reset latched faults                */
    EMS_CMD_POWER_SAVE     = 5,  /* Enter power save                    */
    EMS_CMD_SET_LIMITS     = 6   /* Set charge/discharge limits         */
} bms_ems_cmd_type_t;

typedef struct {
    bms_ems_cmd_type_t type;
    int32_t            charge_limit_ma;    /* for SET_LIMITS             */
    int32_t            discharge_limit_ma; /* for SET_LIMITS             */
    uint32_t           timestamp_ms;
} bms_ems_command_t;

#endif /* BMS_TYPES_H */
