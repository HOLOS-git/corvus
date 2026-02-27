/**
 * corvus_bms.h -- Corvus Orca ESS Battery Management System
 *
 * Public API header: structs, enums, function prototypes.
 *
 * Independent simulation of Orca ESS interface behaviors for integration
 * testing and educational purposes. Not affiliated with, endorsed by, or
 * derived from Corvus Energy's proprietary software.
 *
 * Reference: Corvus Energy Orca ESS integrator documentation
 *
 * Pure C99, no dynamic allocation, fixed-size arrays.
 */

#ifndef CORVUS_BMS_H
#define CORVUS_BMS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =====================================================================
 * COMPILE-TIME CONSTANTS -- Table 13, Section 1.3, RESEARCH.md
 * ===================================================================== */

/* Table 13: Alarm threshold values */
#define BMS_SE_OVER_VOLTAGE_FAULT      4.225   /* V, 5s delay */
#define BMS_SE_UNDER_VOLTAGE_FAULT     3.000   /* V, 5s delay */
#define BMS_SE_OVER_TEMP_FAULT        65.0     /* °C, 5s delay */
#define BMS_SE_OVER_VOLTAGE_WARNING    4.210   /* V, 5s delay */
#define BMS_SE_UNDER_VOLTAGE_WARNING   3.200   /* V, 5s delay */
#define BMS_SE_OVER_TEMP_WARNING      60.0     /* °C, 5s delay */

/* Warning clear thresholds (hysteresis deadband) */
#define BMS_SE_OV_WARN_CLEAR           4.190   /* V -- 20 mV deadband */
#define BMS_SE_UV_WARN_CLEAR           3.220   /* V -- 20 mV deadband */
#define BMS_SE_OT_WARN_CLEAR          57.0     /* °C -- 3°C deadband */

/* Hardware safety -- Table 13 */
#define BMS_HW_SAFETY_OVER_VOLTAGE     4.300   /* V, 1s */
#define BMS_HW_SAFETY_UNDER_VOLTAGE    2.700   /* V, 1s */
#define BMS_HW_SAFETY_OVER_TEMP       70.0     /* °C, 5s */

/* Section 7.2: voltage match for connection */
#define BMS_VOLTAGE_MATCH_PER_MODULE   1.2     /* V per module */

/* Pack parameters -- Orca configuration */
#define BMS_NUM_MODULES               22
#define BMS_CELLS_PER_MODULE          14       /* 14 SE per module */
#define BMS_NOMINAL_CAPACITY_AH      128.0
#define BMS_NUM_CELLS_SERIES          (BMS_NUM_MODULES * BMS_CELLS_PER_MODULE) /* 308 */

/* Thermal parameters -- RESEARCH.md
 * Composite: 70% cell mass (1050 J/kg/K) + 30% non-cell (500 J/kg/K)
 * 22 × 42 kg cells × 1050 + (22 × 18 kg + 200 kg) × 500 ≈ 1,268,000 J/°C */
#define BMS_THERMAL_MASS         1268000.0     /* J/°C */
#define BMS_THERMAL_COOLING_COEFF  800.0       /* W/°C */
#define BMS_AMBIENT_TEMP            40.0       /* °C */

/* Pre-charge timing -- Table 16 */
#define BMS_PRECHARGE_DURATION       5.0       /* seconds */

/* Warning minimum hold time */
#define BMS_WARNING_HOLD_TIME       10.0       /* seconds */

/* Fault reset safe-state hold time -- Section 6.3.5 */
#define BMS_FAULT_RESET_HOLD_TIME   60.0       /* seconds */

/* Coulombic efficiency -- typical NMC 622 */
#define BMS_COULOMBIC_EFFICIENCY     0.998

/* Minimum temperature floor for thermal model */
#define BMS_MIN_TEMPERATURE        -40.0       /* °C */

/* Upper temperature clamp -- above this, thermal runaway makes model meaningless */
#define BMS_MAX_TEMPERATURE        200.0       /* °C */

/* Large-dt guard: subdivide steps larger than this */
#define BMS_MAX_DT                  10.0       /* seconds */

/* Fault timer leaky integrator decay rate (1/s) */
#define BMS_FAULT_TIMER_DECAY_RATE   0.5

/* Kirchhoff solver minimum conductance threshold (S) */
#define BMS_MIN_CONDUCTANCE          1e-12

/* Post-solve current limit tolerance (fraction) */
#define BMS_CURRENT_LIMIT_TOLERANCE  0.01

/* Array sizing */
#define BMS_MAX_PACKS                8         /* max packs per array */

/* Fault message buffer */
#define BMS_MSG_LEN                256

/* =====================================================================
 * ENUMS -- Section 7.1, Table 15: Pack Operation Modes
 * ===================================================================== */

typedef enum {
    BMS_MODE_OFF        = 0,   /* 7.1.7 */
    BMS_MODE_POWER_SAVE = 1,   /* 7.1.2 */
    BMS_MODE_FAULT      = 2,   /* 7.1.3 */
    BMS_MODE_READY      = 3,   /* 7.1.1 */
    BMS_MODE_CONNECTING = 4,   /* 7.1.4 */
    BMS_MODE_CONNECTED  = 5,   /* 7.1.5 */
    BMS_MODE_NOT_READY  = 6    /* 7.1.6 */
} bms_pack_mode_t;

/* =====================================================================
 * STRUCTS
 * ===================================================================== */

/**
 * VirtualPack -- equivalent-circuit battery model.
 * OCV(SoC) + R(SoC, T), coulomb counting, first-order thermal.
 */
typedef struct {
    int    pack_id;
    int    num_modules;
    int    cells_per_module;
    double capacity_ah;

    double soc;              /* 0.0 .. 1.0 */
    double temperature;      /* °C */
    double current;          /* A, positive = charging */
    double cell_voltage;     /* V per cell */
    double pack_voltage;     /* V total */
} corvus_pack_t;

/**
 * PackController -- 7-mode state machine, alarms, current limits.
 */
typedef struct {
    corvus_pack_t  pack;
    bms_pack_mode_t mode;
    bool           contactors_closed;

    double charge_current_limit;
    double discharge_current_limit;

    bool   has_warning;
    bool   has_fault;
    bool   fault_latched;
    bool   hw_fault_latched;
    char   warning_message[BMS_MSG_LEN];
    char   fault_message[BMS_MSG_LEN];

    /* SE alarm delay timers (5s each per Table 13) */
    double ov_fault_timer;
    double uv_fault_timer;
    double ot_fault_timer;
    double ov_warn_timer;
    double uv_warn_timer;
    double ot_warn_timer;

    /* HW safety delay timers -- Table 13 */
    double hw_ov_timer;
    double hw_uv_timer;
    double hw_ot_timer;

    /* Overcurrent timers -- Table 13 */
    double oc_fault_timer;
    double oc_warn_timer;

    /* Warning hold timer */
    double warning_active_time;

    /* Pre-charge timer */
    double precharge_timer;

    /* Fault reset safe-state accumulator */
    double time_in_safe_state;
} corvus_controller_t;

/**
 * Current limit pair (charge, discharge) in amps.
 */
typedef struct {
    double charge;
    double discharge;
} bms_current_limit_t;

/**
 * ArrayController -- manages multiple pack controllers on a shared DC bus.
 */
typedef struct {
    corvus_controller_t controllers[BMS_MAX_PACKS];
    int                 num_packs;
    double              bus_voltage;
    double              array_charge_limit;
    double              array_discharge_limit;
} corvus_array_t;

/* =====================================================================
 * PACK PHYSICS API
 * ===================================================================== */

/** Initialize a virtual pack with given id, SoC, and temperature. */
void corvus_pack_init(corvus_pack_t *pack, int pack_id,
                      double soc, double temperature);

/** Open-circuit voltage per cell from SoC (24-point NMC 622 curve). */
double corvus_ocv_from_soc(double soc);

/** Piecewise dOCV/dT for NMC 622 (V/K). */
double corvus_docv_dt(double soc);

/** Module resistance in Ω from 2D R(T, SoC) bilinear interpolation. */
double corvus_module_resistance(double temp, double soc);

/** Pack resistance in Ω (22 modules in series). */
double corvus_pack_resistance(double temp, double soc);

/** Advance the pack model by dt seconds. */
void corvus_pack_step(corvus_pack_t *pack, double dt, double current,
                      bool contactors_closed, double external_heat);

/* =====================================================================
 * CURRENT LIMIT API -- Figures 28, 29, 30
 * ===================================================================== */

/** Figure 28: Temperature-based current limit. */
bms_current_limit_t corvus_temp_current_limit(double temp, double cap);

/** Figure 29: SoC-based current limit. */
bms_current_limit_t corvus_soc_current_limit(double soc, double cap);

/** Figure 30: SEV (cell voltage) based current limit. */
bms_current_limit_t corvus_sev_current_limit(double cell_v, double cap);

/* =====================================================================
 * CONTROLLER API
 * ===================================================================== */

/** Initialize a pack controller for the given pack. */
void corvus_controller_init(corvus_controller_t *ctrl, int pack_id,
                            double soc, double temperature);

/** Section 7.2: Request connection to bus. Returns true if accepted. */
bool corvus_controller_request_connect(corvus_controller_t *ctrl,
                                       double bus_voltage, bool for_charge);

/** Complete pre-charge and close contactors. */
bool corvus_controller_complete_connection(corvus_controller_t *ctrl,
                                           double bus_voltage);

/** Request disconnect. */
void corvus_controller_request_disconnect(corvus_controller_t *ctrl);

/** Section 6.3.5: Manual fault reset. Returns true if successful. */
bool corvus_controller_manual_fault_reset(corvus_controller_t *ctrl);

/** Control loop step -- computes limits, checks alarms, advances pre-charge. */
void corvus_controller_step(corvus_controller_t *ctrl, double dt,
                            double bus_voltage);

/* =====================================================================
 * ARRAY CONTROLLER API
 * ===================================================================== */

/** Initialize array with given number of packs. */
void corvus_array_init(corvus_array_t *array, int num_packs,
                       const int *pack_ids, const double *socs,
                       const double *temperatures);

/** Connect first pack (lowest SoC for charge, highest for discharge). */
void corvus_array_connect_first(corvus_array_t *array, bool for_charge);

/** Connect remaining packs simultaneously. */
void corvus_array_connect_remaining(corvus_array_t *array, bool for_charge);

/** Disconnect all packs. */
void corvus_array_disconnect_all(corvus_array_t *array);

/** Manual fault reset on all faulted packs. */
void corvus_array_reset_all_faults(corvus_array_t *array);

/** Update bus voltage estimate when no packs are connected. */
void corvus_array_update_bus_voltage(corvus_array_t *array);

/** Compute array-level current limits (Section 7.4). */
void corvus_array_compute_limits(corvus_array_t *array);

/**
 * Main array step: step controllers, solve currents, step physics.
 * external_heat: array of per-pack external heat (W), or NULL.
 */
void corvus_array_step(corvus_array_t *array, double dt,
                       double requested_current,
                       const double *external_heat);

/** Return name string for a pack mode enum value. */
const char *bms_mode_name(bms_pack_mode_t mode);

/** Validate that all pack_ids in an array are unique. Returns true if valid. */
bool corvus_validate_unique_pack_ids(const int *pack_ids, int num_packs);

#ifdef __cplusplus
}
#endif

#endif /* CORVUS_BMS_H */
