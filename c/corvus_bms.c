/**
 * corvus_bms.c -- Corvus Orca ESS Battery Management System
 *
 * Core BMS implementation: VirtualPack, PackController, ArrayController.
 *
 * Pure C99, no dynamic allocation, no external dependencies beyond math.h.
 *
 * Reference: Corvus Energy Orca ESS integrator documentation
 */

#include "corvus_bms.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* =====================================================================
 * INTERNAL HELPERS
 * ===================================================================== */

static inline double clamp_d(double x, double lo, double hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline double max_d(double a, double b) { return a > b ? a : b; }
static inline double min_d(double a, double b) { return a < b ? a : b; }

/**
 * Linear interpolation on a breakpoint table.
 * bp[] must be monotonically increasing, n >= 2.
 * Input is clamped to [bp[0], bp[n-1]].
 */
static double linterp(const double *bp, const double *val, int n, double x)
{
    x = clamp_d(x, bp[0], bp[n - 1]);

    /* Binary search for bracket */
    int lo = 0, hi = n - 2;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (bp[mid] <= x)
            lo = mid;
        else
            hi = mid - 1;
    }

    double span = bp[lo + 1] - bp[lo];
    if (span < 1e-15) return val[lo];
    double frac = (x - bp[lo]) / span;
    return val[lo] + (val[lo + 1] - val[lo]) * frac;
}

/* =====================================================================
 * RESISTANCE LOOKUP TABLE -- R_module(T, SoC) in mΩ
 * Baseline 3.3 mΩ/module at 25°C mid-SoC (Corvus manual p.32)
 * Temperature/SoC variation from NMC pouch cell literature (RESEARCH.md)
 * ===================================================================== */

#define R_NUM_TEMPS 6
#define R_NUM_SOCS  7

static const double r_temps[R_NUM_TEMPS] = {
    -10.0, 0.0, 10.0, 25.0, 35.0, 45.0
};

static const double r_socs[R_NUM_SOCS] = {
    0.05, 0.20, 0.35, 0.50, 0.65, 0.80, 0.95
};

/* mΩ per module -- rows=SoC, cols=Temp
 * U-shaped impedance vs SoC: minimum at 50% (optimal intercalation gradient),
 * higher at extremes (depleted anode at low SoC, full cathode at high SoC).
 * Temperature multipliers preserved from NMC pouch cell literature. */
static const double r_table[R_NUM_SOCS][R_NUM_TEMPS] = {
    { 15.3,  9.7,  6.2,  5.0,  4.4,  4.1 },  /* SoC=5%  (high — depleted anode) */
    { 10.9,  7.2,  4.7,  3.6,  3.3,  3.1 },  /* SoC=20% */
    {  9.9,  6.6,  4.3,  3.3,  3.0,  2.8 },  /* SoC=35% */
    {  9.3,  6.2,  4.0,  3.1,  2.8,  2.6 },  /* SoC=50% (minimum) */
    {  9.6,  6.4,  4.2,  3.2,  2.9,  2.7 },  /* SoC=65% */
    { 10.2,  6.8,  4.4,  3.4,  3.1,  2.9 },  /* SoC=80% */
    { 13.5,  8.9,  5.6,  4.2,  3.9,  3.6 },  /* SoC=95% (high — full cathode) */
};

/**
 * Bilinear interpolation of module resistance (mΩ) from R(T, SoC) table.
 */
static double bilinear_interp(double temp, double soc)
{
    double t = clamp_d(temp, r_temps[0], r_temps[R_NUM_TEMPS - 1]);
    double s = clamp_d(soc, r_socs[0], r_socs[R_NUM_SOCS - 1]);

    /* Find bracketing index for temperature */
    int ti = 0;
    for (int i = R_NUM_TEMPS - 2; i >= 0; i--) {
        if (r_temps[i] <= t) { ti = i; break; }
    }
    double t_frac = (t - r_temps[ti]) / (r_temps[ti + 1] - r_temps[ti]);

    /* Find bracketing index for SoC */
    int si = 0;
    for (int i = R_NUM_SOCS - 2; i >= 0; i--) {
        if (r_socs[i] <= s) { si = i; break; }
    }
    double s_frac = (s - r_socs[si]) / (r_socs[si + 1] - r_socs[si]);

    /* Bilinear */
    double r00 = r_table[si][ti];
    double r01 = r_table[si][ti + 1];
    double r10 = r_table[si + 1][ti];
    double r11 = r_table[si + 1][ti + 1];

    double r0 = r00 + (r01 - r00) * t_frac;
    double r1 = r10 + (r11 - r10) * t_frac;
    return r0 + (r1 - r0) * s_frac;
}

double corvus_module_resistance(double temp, double soc)
{
    return bilinear_interp(temp, soc) * 1e-3;
}

double corvus_pack_resistance(double temp, double soc)
{
    return corvus_module_resistance(temp, soc) * BMS_NUM_MODULES;
}

/* =====================================================================
 * OCV vs SoC -- 24-point NMC 622 curve from RESEARCH.md
 * Section 2.3: "SoC is a percentage measure of the charge remaining"
 * ===================================================================== */

#define OCV_NUM_POINTS 24

static const double ocv_soc_bp[OCV_NUM_POINTS] = {
    0.00, 0.02, 0.05, 0.08, 0.10, 0.15, 0.20, 0.25,
    0.30, 0.35, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65,
    0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 0.98, 1.00,
};

static const double ocv_val_bp[OCV_NUM_POINTS] = {
    3.000, 3.280, 3.420, 3.480, 3.510, 3.555, 3.590, 3.610,
    3.625, 3.638, 3.650, 3.662, 3.675, 3.690, 3.710, 3.735,
    3.765, 3.800, 3.845, 3.900, 3.960, 4.030, 4.100, 4.190,
};

double corvus_ocv_from_soc(double soc)
{
    return linterp(ocv_soc_bp, ocv_val_bp, OCV_NUM_POINTS,
                   clamp_d(soc, 0.0, 1.0));
}

/**
 * 7-segment piecewise dOCV/dT for NMC 622 (V/K).
 * Approximate values from literature for finer entropic heating.
 */
double corvus_docv_dt(double soc)
{
    if (soc < 0.10)
        return -0.10e-3;
    else if (soc < 0.25)
        return -0.25e-3;
    else if (soc < 0.50)
        return -0.45e-3;
    else if (soc < 0.70)
        return -0.35e-3;
    else if (soc < 0.85)
        return -0.15e-3;
    else if (soc < 0.95)
        return  0.05e-3;
    else
        return  0.15e-3;
}

/* =====================================================================
 * CURRENT LIMIT CURVES -- Figures 28, 29, 30 (RESEARCH.md breakpoints)
 * All return C-rates as positive magnitudes.
 * ===================================================================== */

/* Figure 28: Temperature-based current limit */
static const double temp_charge_bp[] = { -25, 0, 5, 15, 35, 45, 55, 65 };
static const double temp_charge_cr[] = { 0.0, 0.0, 0.0, 3.0, 3.0, 2.0, 0.0, 0.0 };
#define TEMP_CHARGE_N ((int)(sizeof(temp_charge_bp)/sizeof(temp_charge_bp[0])))

static const double temp_disch_bp[] = {
    -25, -15, -10, -5, 0, 5, 10, 25, 30, 35, 45, 55, 60, 65, 70
};
static const double temp_disch_cr[] = {
    0.2, 0.2, 1.0, 1.5, 2.0, 4.5, 5.0, 5.0, 4.5, 4.0, 3.8, 3.8, 0.2, 0.2, 0.0
};
#define TEMP_DISCH_N ((int)(sizeof(temp_disch_bp)/sizeof(temp_disch_bp[0])))

/* Figure 29: SoC-based current limit (BOL) */
static const double soc_charge_bp[] = { 0.0, 0.85, 0.90, 0.95, 1.00 };
static const double soc_charge_cr[] = { 3.0, 3.0, 2.0, 1.0, 0.5 };
#define SOC_CHARGE_N ((int)(sizeof(soc_charge_bp)/sizeof(soc_charge_bp[0])))

static const double soc_disch_bp[] = {
    0.00, 0.02, 0.05, 0.08, 0.10, 0.15, 0.20, 0.50, 1.00
};
static const double soc_disch_cr[] = {
    1.0, 1.0, 2.2, 2.2, 4.0, 4.0, 5.0, 5.0, 5.0
};
#define SOC_DISCH_N ((int)(sizeof(soc_disch_bp)/sizeof(soc_disch_bp[0])))

/* Figure 30: SEV (cell voltage) based current limit */
static const double sev_charge_bp[] = { 3.000, 4.100, 4.200 };
static const double sev_charge_cr[] = { 3.0, 3.0, 0.0 };
#define SEV_CHARGE_N ((int)(sizeof(sev_charge_bp)/sizeof(sev_charge_bp[0])))

static const double sev_disch_bp[] = {
    3.000, 3.200, 3.300, 3.400, 3.450, 3.550, 4.200
};
static const double sev_disch_cr[] = {
    0.0, 0.0, 2.0, 2.5, 3.8, 5.0, 5.0
};
#define SEV_DISCH_N ((int)(sizeof(sev_disch_bp)/sizeof(sev_disch_bp[0])))

bms_current_limit_t corvus_temp_current_limit(double temp, double cap)
{
    bms_current_limit_t lim;
    lim.charge    = max_d(0.0, linterp(temp_charge_bp, temp_charge_cr, TEMP_CHARGE_N, temp) * cap);
    lim.discharge = max_d(0.0, linterp(temp_disch_bp,  temp_disch_cr,  TEMP_DISCH_N,  temp) * cap);
    return lim;
}

bms_current_limit_t corvus_soc_current_limit(double soc, double cap)
{
    bms_current_limit_t lim;
    lim.charge    = max_d(0.0, linterp(soc_charge_bp, soc_charge_cr, SOC_CHARGE_N, soc) * cap);
    lim.discharge = max_d(0.0, linterp(soc_disch_bp,  soc_disch_cr,  SOC_DISCH_N,  soc) * cap);
    return lim;
}

bms_current_limit_t corvus_sev_current_limit(double cell_v, double cap)
{
    bms_current_limit_t lim;
    lim.charge    = max_d(0.0, linterp(sev_charge_bp, sev_charge_cr, SEV_CHARGE_N, cell_v) * cap);
    lim.discharge = max_d(0.0, linterp(sev_disch_bp,  sev_disch_cr,  SEV_DISCH_N,  cell_v) * cap);
    return lim;
}

/* =====================================================================
 * VIRTUAL PACK IMPLEMENTATION
 * ===================================================================== */

static void pack_update_voltage(corvus_pack_t *p)
{
    double ocv = corvus_ocv_from_soc(p->soc);
    double r_total = corvus_pack_resistance(p->temperature, p->soc);
    int n_cells = p->num_modules * p->cells_per_module;
    if (n_cells <= 0) {
        p->cell_voltage = ocv;
        p->pack_voltage = 0.0;
        return;
    }
    p->cell_voltage = ocv + p->current * r_total / (double)n_cells;
    p->pack_voltage = p->cell_voltage * (double)n_cells;
}

void corvus_pack_init(corvus_pack_t *pack, int pack_id,
                      double soc, double temperature)
{
    memset(pack, 0, sizeof(*pack));
    pack->pack_id         = pack_id;
    pack->num_modules     = BMS_NUM_MODULES;
    pack->cells_per_module = BMS_CELLS_PER_MODULE;
    pack->capacity_ah     = BMS_NOMINAL_CAPACITY_AH;
    pack->soc             = clamp_d(soc, 0.0, 1.0);
    pack->temperature     = temperature;
    pack->current         = 0.0;
    pack_update_voltage(pack);
}

bool corvus_validate_unique_pack_ids(const int *pack_ids, int num_packs)
{
    if (!pack_ids || num_packs <= 0)
        return num_packs == 0;
    for (int i = 0; i < num_packs; i++) {
        for (int j = i + 1; j < num_packs; j++) {
            if (pack_ids[i] == pack_ids[j])
                return false;
        }
    }
    return true;
}

/**
 * Single sub-step of pack physics (no dt subdivision).
 */
static void pack_step_internal(corvus_pack_t *pack, double dt, double current,
                               bool contactors_closed, double external_heat)
{
    if (!contactors_closed)
        pack->current = 0.0;
    else
        pack->current = current;

    /* Coulomb counting -- Section 2.3 */
    double effective_current;
    if (pack->current > 0.0)  /* charging */
        effective_current = pack->current * BMS_COULOMBIC_EFFICIENCY;
    else  /* discharging */
        effective_current = pack->current;
    double delta_soc = (effective_current * dt) / (pack->capacity_ah * 3600.0);
    pack->soc = clamp_d(pack->soc + delta_soc, 0.0, 1.0);

    /* First-order thermal: dT/dt = (I²R + Q_rev + external - cooling) / C_thermal */
    double r_total = corvus_pack_resistance(pack->temperature, pack->soc);
    int n_cells = pack->num_modules * pack->cells_per_module;
    double t_kelvin = pack->temperature + 273.15;
    double q_rev = pack->current * t_kelvin * corvus_docv_dt(pack->soc) * n_cells;
    double heat_gen = pack->current * pack->current * r_total + q_rev + external_heat;
    double cooling = BMS_THERMAL_COOLING_COEFF * (pack->temperature - BMS_AMBIENT_TEMP);
    pack->temperature += (heat_gen - cooling) / BMS_THERMAL_MASS * dt;
    if (pack->temperature < BMS_MIN_TEMPERATURE)
        pack->temperature = BMS_MIN_TEMPERATURE;
    if (pack->temperature > BMS_MAX_TEMPERATURE)
        pack->temperature = BMS_MAX_TEMPERATURE;

    pack_update_voltage(pack);
}

int corvus_pack_step(corvus_pack_t *pack, double dt, double current,
                     bool contactors_closed, double external_heat)
{
    if (dt <= 0.0) return -1;
    /* Large-dt guard: subdivide into sub-steps of at most BMS_MAX_DT */
    double remaining = dt;
    while (remaining > 0.0) {
        double sub_dt = remaining < BMS_MAX_DT ? remaining : BMS_MAX_DT;
        pack_step_internal(pack, sub_dt, current, contactors_closed, external_heat);
        remaining -= sub_dt;
    }
    return 0;
}

/* =====================================================================
 * PACK CONTROLLER IMPLEMENTATION
 * ===================================================================== */

static void append_fault_msg(char *buf, int buflen, const char *msg)
{
    if ((int)strlen(buf) >= buflen - 2)
        return;
    if (buf[0] == '\0') {
        snprintf(buf, buflen, "%s", msg);
    } else if (strstr(buf, msg) == NULL) {
        int len = (int)strlen(buf);
        snprintf(buf + len, buflen - len, "; %s", msg);
    }
}

static void trigger_hw_fault(corvus_controller_t *ctrl, const char *message)
{
    ctrl->has_fault = true;
    ctrl->fault_latched = true;
    ctrl->hw_fault_latched = true;
    append_fault_msg(ctrl->fault_message, BMS_MSG_LEN, message);
    ctrl->contactors_closed = false;
    ctrl->mode = BMS_MODE_FAULT;
    ctrl->charge_current_limit = 0.0;
    ctrl->discharge_current_limit = 0.0;
}

static void trigger_sw_fault(corvus_controller_t *ctrl, const char *message)
{
    ctrl->has_fault = true;
    ctrl->fault_latched = true;
    append_fault_msg(ctrl->fault_message, BMS_MSG_LEN, message);
    ctrl->contactors_closed = false;
    ctrl->mode = BMS_MODE_FAULT;
    ctrl->charge_current_limit = 0.0;
    ctrl->discharge_current_limit = 0.0;
}

/**
 * Section 6.2: Hardware Safety System -- INDEPENDENT of software faults.
 * Runs even when fault_latched is True.
 * Table 13: HW OV/UV = 1s delay, HW OT = 5s delay.
 */
static void check_hw_safety(corvus_controller_t *ctrl, double dt)
{
    double v = ctrl->pack.cell_voltage;
    double t = ctrl->pack.temperature;
    char msg[BMS_MSG_LEN];

    if (v >= BMS_HW_SAFETY_OVER_VOLTAGE) {
        ctrl->hw_ov_timer += dt;
        if (ctrl->hw_ov_timer >= 1.0) {
            snprintf(msg, sizeof(msg),
                     "HW SAFETY: voltage %.3fV >= %.3fV", v, BMS_HW_SAFETY_OVER_VOLTAGE);
            trigger_hw_fault(ctrl, msg);
        }
    } else {
        ctrl->hw_ov_timer = max_d(0.0, ctrl->hw_ov_timer - dt * BMS_FAULT_TIMER_DECAY_RATE);
    }

    if (v <= BMS_HW_SAFETY_UNDER_VOLTAGE) {
        ctrl->hw_uv_timer += dt;
        if (ctrl->hw_uv_timer >= 1.0) {
            snprintf(msg, sizeof(msg),
                     "HW SAFETY: voltage %.3fV <= %.3fV", v, BMS_HW_SAFETY_UNDER_VOLTAGE);
            trigger_hw_fault(ctrl, msg);
        }
    } else {
        ctrl->hw_uv_timer = max_d(0.0, ctrl->hw_uv_timer - dt * BMS_FAULT_TIMER_DECAY_RATE);
    }

    if (t >= BMS_HW_SAFETY_OVER_TEMP) {
        ctrl->hw_ot_timer += dt;
        if (ctrl->hw_ot_timer >= 5.0) {
            snprintf(msg, sizeof(msg),
                     "HW SAFETY: temp %.1f°C >= %.1f°C", t, BMS_HW_SAFETY_OVER_TEMP);
            trigger_hw_fault(ctrl, msg);
        }
    } else {
        ctrl->hw_ot_timer = max_d(0.0, ctrl->hw_ot_timer - dt * BMS_FAULT_TIMER_DECAY_RATE);
    }
}

/**
 * Section 6.3.1 + Table 13: Check alarm conditions with delays.
 */
static void check_alarms(corvus_controller_t *ctrl, double dt)
{
    double v = ctrl->pack.cell_voltage;
    double t = ctrl->pack.temperature;
    char msg[BMS_MSG_LEN];

    /* -- WARNINGS with hysteresis -- */
    bool warn_ov = false, warn_uv = false, warn_ot = false, warn_oc = false;

    if (v >= BMS_SE_OVER_VOLTAGE_WARNING) {
        ctrl->ov_warn_timer += dt;
        if (ctrl->ov_warn_timer >= 5.0)
            warn_ov = true;
    } else if (v < BMS_SE_OV_WARN_CLEAR) {
        ctrl->ov_warn_timer = 0.0;
    }

    if (v <= BMS_SE_UNDER_VOLTAGE_WARNING) {
        ctrl->uv_warn_timer += dt;
        if (ctrl->uv_warn_timer >= 5.0)
            warn_uv = true;
    } else if (v > BMS_SE_UV_WARN_CLEAR) {
        ctrl->uv_warn_timer = 0.0;
    }

    if (t >= BMS_SE_OVER_TEMP_WARNING) {
        ctrl->ot_warn_timer += dt;
        if (ctrl->ot_warn_timer >= 5.0)
            warn_ot = true;
    } else if (t < BMS_SE_OT_WARN_CLEAR) {
        ctrl->ot_warn_timer = 0.0;
    }

    /* -- OVERCURRENT -- Table 13 */
    bms_current_limit_t tc_lim = corvus_temp_current_limit(t, ctrl->pack.capacity_ah);
    double i = ctrl->pack.current;
    bool oc_charge    = i > 1.05 * tc_lim.charge + 5.0;
    bool oc_discharge = i < -(1.05 * tc_lim.discharge - 5.0);

    if (oc_charge || oc_discharge) {
        ctrl->oc_warn_timer += dt;
        if (ctrl->oc_warn_timer >= 10.0)
            warn_oc = true;
    } else {
        ctrl->oc_warn_timer = max_d(0.0, ctrl->oc_warn_timer - dt * BMS_FAULT_TIMER_DECAY_RATE);
    }

    bool any_warn = warn_ov || warn_uv || warn_ot || warn_oc;

    /* Update warning state with hold time */
    if (any_warn) {
        ctrl->has_warning = true;
        ctrl->warning_message[0] = '\0';
        if (warn_ov) {
            snprintf(msg, sizeof(msg), "SE OV warning: %.3fV", v);
            append_fault_msg(ctrl->warning_message, BMS_MSG_LEN, msg);
        }
        if (warn_uv) {
            snprintf(msg, sizeof(msg), "SE UV warning: %.3fV", v);
            append_fault_msg(ctrl->warning_message, BMS_MSG_LEN, msg);
        }
        if (warn_ot) {
            snprintf(msg, sizeof(msg), "SE OT warning: %.1f°C", t);
            append_fault_msg(ctrl->warning_message, BMS_MSG_LEN, msg);
        }
        if (warn_oc) {
            snprintf(msg, sizeof(msg), "OC warning: I=%.1fA", i);
            append_fault_msg(ctrl->warning_message, BMS_MSG_LEN, msg);
        }
        ctrl->warning_active_time = 0.0;
    } else if (ctrl->has_warning) {
        ctrl->warning_active_time += dt;
        if (ctrl->warning_active_time >= BMS_WARNING_HOLD_TIME) {
            ctrl->has_warning = false;
            ctrl->warning_message[0] = '\0';
            ctrl->warning_active_time = 0.0;
        }
    }

    /* -- OC fault (5s) -- ONLY at T < 0°C AND charging per Table 13 */
    if (t < 0.0 && oc_charge) {
        ctrl->oc_fault_timer += dt;
        if (ctrl->oc_fault_timer >= 5.0 && !ctrl->fault_latched) {
            snprintf(msg, sizeof(msg),
                     "OC fault: I=%.1fA at T=%.1f°C (charge at sub-zero)", i, t);
            trigger_sw_fault(ctrl, msg);
        }
    } else {
        ctrl->oc_fault_timer = max_d(0.0, ctrl->oc_fault_timer - dt * BMS_FAULT_TIMER_DECAY_RATE);
    }

    /* -- SE FAULTS (5s delay each) -- leaky integrator decay */
    if (v >= BMS_SE_OVER_VOLTAGE_FAULT) {
        ctrl->ov_fault_timer += dt;
        if (ctrl->ov_fault_timer >= 5.0 && !ctrl->fault_latched) {
            snprintf(msg, sizeof(msg),
                     "SE OV fault: %.3fV >= %.3fV", v, BMS_SE_OVER_VOLTAGE_FAULT);
            trigger_sw_fault(ctrl, msg);
        }
    } else {
        ctrl->ov_fault_timer = max_d(0.0, ctrl->ov_fault_timer - dt * BMS_FAULT_TIMER_DECAY_RATE);
    }

    if (v <= BMS_SE_UNDER_VOLTAGE_FAULT) {
        ctrl->uv_fault_timer += dt;
        if (ctrl->uv_fault_timer >= 5.0 && !ctrl->fault_latched) {
            snprintf(msg, sizeof(msg),
                     "SE UV fault: %.3fV <= %.3fV", v, BMS_SE_UNDER_VOLTAGE_FAULT);
            trigger_sw_fault(ctrl, msg);
        }
    } else {
        ctrl->uv_fault_timer = max_d(0.0, ctrl->uv_fault_timer - dt * BMS_FAULT_TIMER_DECAY_RATE);
    }

    if (t >= BMS_SE_OVER_TEMP_FAULT) {
        ctrl->ot_fault_timer += dt;
        if (ctrl->ot_fault_timer >= 5.0 && !ctrl->fault_latched) {
            snprintf(msg, sizeof(msg),
                     "SE OT fault: %.1f°C >= %.1f°C", t, BMS_SE_OVER_TEMP_FAULT);
            trigger_sw_fault(ctrl, msg);
        }
    } else {
        ctrl->ot_fault_timer = max_d(0.0, ctrl->ot_fault_timer - dt * BMS_FAULT_TIMER_DECAY_RATE);
    }
}

/**
 * Accumulate time in safe state for fault reset hold-time requirement.
 */
static void update_safe_state_timer(corvus_controller_t *ctrl, double dt)
{
    double v = ctrl->pack.cell_voltage;
    double t = ctrl->pack.temperature;
    if (v < BMS_SE_OVER_VOLTAGE_FAULT && v > BMS_SE_UNDER_VOLTAGE_FAULT
        && t < BMS_SE_OVER_TEMP_FAULT && t < BMS_HW_SAFETY_OVER_TEMP) {
        ctrl->time_in_safe_state += dt;
    } else {
        ctrl->time_in_safe_state = 0.0;
    }
}

void corvus_controller_init(corvus_controller_t *ctrl, int pack_id,
                            double soc, double temperature)
{
    /* Belt-and-suspenders: zero everything first, then set explicitly.
     * Every field is initialized below — memset is a safety net only. */
    memset(ctrl, 0, sizeof(*ctrl));

    corvus_pack_init(&ctrl->pack, pack_id, soc, temperature);
    ctrl->mode = BMS_MODE_READY;
    ctrl->contactors_closed = false;

    ctrl->charge_current_limit = BMS_NOMINAL_CAPACITY_AH;
    ctrl->discharge_current_limit = BMS_NOMINAL_CAPACITY_AH;

    ctrl->has_warning = false;
    ctrl->has_fault = false;
    ctrl->fault_latched = false;
    ctrl->hw_fault_latched = false;
    ctrl->warning_message[0] = '\0';
    ctrl->fault_message[0] = '\0';

    ctrl->ov_fault_timer = 0.0;
    ctrl->uv_fault_timer = 0.0;
    ctrl->ot_fault_timer = 0.0;
    ctrl->ov_warn_timer = 0.0;
    ctrl->uv_warn_timer = 0.0;
    ctrl->ot_warn_timer = 0.0;
    ctrl->hw_ov_timer = 0.0;
    ctrl->hw_uv_timer = 0.0;
    ctrl->hw_ot_timer = 0.0;
    ctrl->oc_fault_timer = 0.0;
    ctrl->oc_warn_timer = 0.0;
    ctrl->warning_active_time = 0.0;
    ctrl->precharge_timer = 0.0;
    ctrl->time_in_safe_state = 0.0;
}

bool corvus_controller_request_connect(corvus_controller_t *ctrl,
                                       double bus_voltage, bool for_charge)
{
    (void)for_charge;
    if (ctrl->mode != BMS_MODE_READY)
        return false;

    double max_delta = BMS_VOLTAGE_MATCH_PER_MODULE * ctrl->pack.num_modules;
    double actual_delta = fabs(ctrl->pack.pack_voltage - bus_voltage);
    if (actual_delta > max_delta)
        return false;

    ctrl->mode = BMS_MODE_CONNECTING;
    ctrl->precharge_timer = 0.0;
    return true;
}

bool corvus_controller_complete_connection(corvus_controller_t *ctrl,
                                           double bus_voltage)
{
    if (ctrl->mode != BMS_MODE_CONNECTING)
        return false;

    double max_delta = BMS_VOLTAGE_MATCH_PER_MODULE * ctrl->pack.num_modules;
    if (fabs(ctrl->pack.pack_voltage - bus_voltage) > max_delta) {
        ctrl->mode = BMS_MODE_READY;
        return false;
    }

    ctrl->mode = BMS_MODE_CONNECTED;
    ctrl->contactors_closed = true;
    return true;
}

void corvus_controller_request_disconnect(corvus_controller_t *ctrl)
{
    if (ctrl->mode == BMS_MODE_CONNECTED || ctrl->mode == BMS_MODE_CONNECTING) {
        ctrl->contactors_closed = false;
        ctrl->mode = BMS_MODE_READY;
    }
}

bool corvus_controller_manual_fault_reset(corvus_controller_t *ctrl)
{
    if (!ctrl->fault_latched)
        return true;

    double v = ctrl->pack.cell_voltage;
    double t = ctrl->pack.temperature;

    /* Conditions must be safe */
    if (!(v < BMS_SE_OVER_VOLTAGE_FAULT && v > BMS_SE_UNDER_VOLTAGE_FAULT
          && t < BMS_SE_OVER_TEMP_FAULT)) {
        ctrl->time_in_safe_state = 0.0;
        return false;
    }

    /* Must have held safe state for FAULT_RESET_HOLD_TIME */
    if (ctrl->time_in_safe_state < BMS_FAULT_RESET_HOLD_TIME)
        return false;

    ctrl->fault_latched = false;
    ctrl->hw_fault_latched = false;
    ctrl->has_fault = false;
    ctrl->fault_message[0] = '\0';
    ctrl->mode = BMS_MODE_READY;

    /* Reset all timers */
    ctrl->ov_fault_timer = 0.0;
    ctrl->uv_fault_timer = 0.0;
    ctrl->ot_fault_timer = 0.0;
    ctrl->hw_ov_timer = 0.0;
    ctrl->hw_uv_timer = 0.0;
    ctrl->hw_ot_timer = 0.0;
    ctrl->oc_fault_timer = 0.0;
    ctrl->oc_warn_timer = 0.0;
    ctrl->time_in_safe_state = 0.0;
    return true;
}

void corvus_controller_step(corvus_controller_t *ctrl, double dt,
                            double bus_voltage)
{
    /* HW safety ALWAYS runs, independent of fault state */
    check_hw_safety(ctrl, dt);

    /* SW alarms */
    check_alarms(ctrl, dt);

    /* Safe state timer for fault reset */
    update_safe_state_timer(ctrl, dt);

    if (ctrl->fault_latched) {
        ctrl->charge_current_limit = 0.0;
        ctrl->discharge_current_limit = 0.0;
        return;
    }

    /* Pre-charge timer */
    if (ctrl->mode == BMS_MODE_CONNECTING) {
        ctrl->precharge_timer += dt;
        if (ctrl->precharge_timer >= BMS_PRECHARGE_DURATION)
            corvus_controller_complete_connection(ctrl, bus_voltage);
    }

    /* Compute current limits: min(temp, soc, sev) -- Section 7.4 */
    bms_current_limit_t tc = corvus_temp_current_limit(ctrl->pack.temperature, ctrl->pack.capacity_ah);
    bms_current_limit_t sc = corvus_soc_current_limit(ctrl->pack.soc, ctrl->pack.capacity_ah);
    bms_current_limit_t vc = corvus_sev_current_limit(ctrl->pack.cell_voltage, ctrl->pack.capacity_ah);

    ctrl->charge_current_limit    = max_d(0.0, min_d(tc.charge,    min_d(sc.charge,    vc.charge)));
    ctrl->discharge_current_limit = max_d(0.0, min_d(tc.discharge, min_d(sc.discharge, vc.discharge)));
}

/* =====================================================================
 * ARRAY CONTROLLER IMPLEMENTATION
 * ===================================================================== */

const char *bms_mode_name(bms_pack_mode_t mode)
{
    switch (mode) {
    case BMS_MODE_OFF:        return "OFF";
    case BMS_MODE_POWER_SAVE: return "POWER_SAVE";
    case BMS_MODE_FAULT:      return "FAULT";
    case BMS_MODE_READY:      return "READY";
    case BMS_MODE_CONNECTING: return "CONNECTING";
    case BMS_MODE_CONNECTED:  return "CONNECTED";
    case BMS_MODE_NOT_READY:  return "NOT_READY";
    default:                  return "UNKNOWN";
    }
}

void corvus_array_init(corvus_array_t *array, int num_packs,
                       const int *pack_ids, const double *socs,
                       const double *temperatures)
{
    memset(array, 0, sizeof(*array));
    if (!pack_ids || !socs || !temperatures || num_packs <= 0) {
        array->num_packs = 0;
        return;
    }
    array->num_packs = num_packs < BMS_MAX_PACKS ? num_packs : BMS_MAX_PACKS;

    /* Validate unique pack IDs */
    if (!corvus_validate_unique_pack_ids(pack_ids, array->num_packs)) {
        fprintf(stderr, "corvus_array_init: duplicate pack_ids detected\n");
        /* Still initialize, but warn -- demo code, not abort */
    }

    for (int i = 0; i < array->num_packs; i++)
        corvus_controller_init(&array->controllers[i], pack_ids[i],
                               socs[i], temperatures[i]);
}

void corvus_array_update_bus_voltage(corvus_array_t *array)
{
    /* Check connected packs first */
    double sum = 0.0;
    int cnt = 0;
    for (int i = 0; i < array->num_packs; i++) {
        if (array->controllers[i].mode == BMS_MODE_CONNECTED) {
            sum += array->controllers[i].pack.pack_voltage;
            cnt++;
        }
    }
    if (cnt > 0) { array->bus_voltage = sum / cnt; return; }

    /* Fall back to ready packs */
    for (int i = 0; i < array->num_packs; i++) {
        if (array->controllers[i].mode == BMS_MODE_READY) {
            sum += array->controllers[i].pack.pack_voltage;
            cnt++;
        }
    }
    if (cnt > 0) array->bus_voltage = sum / cnt;
}

void corvus_array_compute_limits(corvus_array_t *array)
{
    int n = 0;
    double min_c = 1e30, min_d = 1e30;
    for (int i = 0; i < array->num_packs; i++) {
        if (array->controllers[i].mode == BMS_MODE_CONNECTED) {
            n++;
            if (array->controllers[i].charge_current_limit < min_c)
                min_c = array->controllers[i].charge_current_limit;
            if (array->controllers[i].discharge_current_limit < min_d)
                min_d = array->controllers[i].discharge_current_limit;
        }
    }
    if (n == 0) {
        array->array_charge_limit = 0.0;
        array->array_discharge_limit = 0.0;
    } else {
        array->array_charge_limit = min_c * n;
        array->array_discharge_limit = min_d * n;
    }
}

void corvus_array_connect_first(corvus_array_t *array, bool for_charge)
{
    /* Check if any already connected or connecting */
    for (int i = 0; i < array->num_packs; i++) {
        bms_pack_mode_t m = array->controllers[i].mode;
        if (m == BMS_MODE_CONNECTED || m == BMS_MODE_CONNECTING)
            return;
    }

    /* Find best ready pack by SoC */
    int best = -1;
    for (int i = 0; i < array->num_packs; i++) {
        if (array->controllers[i].mode != BMS_MODE_READY)
            continue;
        if (best < 0)
            best = i;
        else if (for_charge && array->controllers[i].pack.soc < array->controllers[best].pack.soc)
            best = i;
        else if (!for_charge && array->controllers[i].pack.soc > array->controllers[best].pack.soc)
            best = i;
    }

    if (best >= 0)
        corvus_controller_request_connect(&array->controllers[best],
                                          array->bus_voltage, for_charge);
}

void corvus_array_connect_remaining(corvus_array_t *array, bool for_charge)
{
    /* Need at least one connected pack first */
    bool has_connected = false;
    for (int i = 0; i < array->num_packs; i++) {
        if (array->controllers[i].mode == BMS_MODE_CONNECTED) {
            has_connected = true;
            break;
        }
    }
    if (!has_connected) return;

    for (int i = 0; i < array->num_packs; i++) {
        if (array->controllers[i].mode == BMS_MODE_READY)
            corvus_controller_request_connect(&array->controllers[i],
                                              array->bus_voltage, for_charge);
    }
}

void corvus_array_disconnect_all(corvus_array_t *array)
{
    for (int i = 0; i < array->num_packs; i++)
        corvus_controller_request_disconnect(&array->controllers[i]);
}

void corvus_array_reset_all_faults(corvus_array_t *array)
{
    for (int i = 0; i < array->num_packs; i++) {
        if (array->controllers[i].fault_latched)
            corvus_controller_manual_fault_reset(&array->controllers[i]);
    }
}

/**
 * Unified Kirchhoff/equalization solver with per-pack limit enforcement.
 *
 * Kirchhoff mode (is_equalization=false):
 *   V_bus = (Σ(OCV_k/R_k) + I_target) / Σ(1/R_k)
 *
 * Equalization mode (is_equalization=true):
 *   target_current = 0, KCL: ΣI_k = 0
 *   V_bus = (Σ(OCV_k/R_k) - clamped_sum) / Σ(1/R_k)
 */
static void solve_currents(corvus_array_t *array,
                           int *conn_idx, int num_conn,
                           double target_current,
                           bool is_equalization,
                           double *pack_currents)
{
    /* Clamp total requested current to array limits (Kirchhoff only) */
    double actual_total;
    if (!is_equalization) {
        if (target_current > 0)
            actual_total = min_d(target_current, array->array_charge_limit);
        else if (target_current < 0)
            actual_total = max_d(target_current, -array->array_discharge_limit);
        else
            actual_total = 0.0;
    } else {
        actual_total = 0.0;
    }

    bool active[BMS_MAX_PACKS];
    double clamped_val[BMS_MAX_PACKS];
    bool is_clamped[BMS_MAX_PACKS];
    for (int i = 0; i < num_conn; i++) {
        active[i] = true;
        clamped_val[i] = 0.0;
        is_clamped[i] = false;
    }

    double residual = actual_total;

    for (int iteration = 0; iteration < num_conn; iteration++) {
        double sum_g = 0.0, sum_ocv_g = 0.0;
        for (int i = 0; i < num_conn; i++) {
            if (!active[i]) continue;
            corvus_controller_t *c = &array->controllers[conn_idx[i]];
            double r = corvus_pack_resistance(c->pack.temperature, c->pack.soc);
            double ocv = corvus_ocv_from_soc(c->pack.soc) * (double)BMS_NUM_CELLS_SERIES;
            sum_g += 1.0 / r;
            sum_ocv_g += ocv / r;
        }
        if (sum_g < BMS_MIN_CONDUCTANCE) break;

        double v_bus;
        if (is_equalization) {
            double clamped_sum = 0.0;
            for (int i = 0; i < num_conn; i++)
                if (is_clamped[i]) clamped_sum += clamped_val[i];
            v_bus = (sum_ocv_g - clamped_sum) / sum_g;
        } else {
            v_bus = (sum_ocv_g + residual) / sum_g;
        }

        bool any_clamped = false;
        for (int i = 0; i < num_conn; i++) {
            if (!active[i]) continue;
            corvus_controller_t *c = &array->controllers[conn_idx[i]];
            double r = corvus_pack_resistance(c->pack.temperature, c->pack.soc);
            double ocv = corvus_ocv_from_soc(c->pack.soc) * (double)BMS_NUM_CELLS_SERIES;
            double i_k = (v_bus - ocv) / r;

            if (i_k > 0 && i_k > c->charge_current_limit) {
                clamped_val[i] = c->charge_current_limit;
                is_clamped[i] = true;
                active[i] = false;
                if (!is_equalization) residual -= c->charge_current_limit;
                any_clamped = true;
            } else if (i_k < 0 && -i_k > c->discharge_current_limit) {
                clamped_val[i] = -c->discharge_current_limit;
                is_clamped[i] = true;
                active[i] = false;
                if (!is_equalization) residual -= (-c->discharge_current_limit);
                any_clamped = true;
            } else {
                pack_currents[i] = i_k;
            }
        }

        if (!any_clamped) {
            array->bus_voltage = v_bus;
            for (int i = 0; i < num_conn; i++)
                if (is_clamped[i]) pack_currents[i] = clamped_val[i];

            if (!is_equalization) {
                /* Post-solve clamp */
                for (int i = 0; i < num_conn; i++) {
                    corvus_controller_t *c = &array->controllers[conn_idx[i]];
                    if (pack_currents[i] > 0 && pack_currents[i] > c->charge_current_limit * (1.0 + BMS_CURRENT_LIMIT_TOLERANCE))
                        pack_currents[i] = c->charge_current_limit;
                    else if (pack_currents[i] < 0 && -pack_currents[i] > c->discharge_current_limit * (1.0 + BMS_CURRENT_LIMIT_TOLERANCE))
                        pack_currents[i] = -c->discharge_current_limit;
                }
            }
            return;
        }
    }

    /* Final solve with remaining active */
    double sum_g = 0.0, sum_ocv_g = 0.0;
    double clamped_sum = 0.0;
    bool has_active = false;
    for (int i = 0; i < num_conn; i++) {
        if (is_clamped[i]) {
            pack_currents[i] = clamped_val[i];
            clamped_sum += clamped_val[i];
            continue;
        }
        has_active = true;
        corvus_controller_t *c = &array->controllers[conn_idx[i]];
        double r = corvus_pack_resistance(c->pack.temperature, c->pack.soc);
        double ocv = corvus_ocv_from_soc(c->pack.soc) * (double)BMS_NUM_CELLS_SERIES;
        sum_g += 1.0 / r;
        sum_ocv_g += ocv / r;
    }
    if (has_active && sum_g > BMS_MIN_CONDUCTANCE) {
        double v_bus;
        if (is_equalization)
            v_bus = (sum_ocv_g - clamped_sum) / sum_g;
        else
            v_bus = (sum_ocv_g + residual) / sum_g;
        array->bus_voltage = v_bus;
        for (int i = 0; i < num_conn; i++) {
            if (is_clamped[i]) continue;
            corvus_controller_t *c = &array->controllers[conn_idx[i]];
            double r = corvus_pack_resistance(c->pack.temperature, c->pack.soc);
            double ocv = corvus_ocv_from_soc(c->pack.soc) * (double)BMS_NUM_CELLS_SERIES;
            pack_currents[i] = (v_bus - ocv) / r;
        }
    } else if (!has_active) {
        double vsum = 0.0;
        for (int i = 0; i < num_conn; i++) {
            corvus_controller_t *c = &array->controllers[conn_idx[i]];
            double r = corvus_pack_resistance(c->pack.temperature, c->pack.soc);
            double ocv = corvus_ocv_from_soc(c->pack.soc) * (double)BMS_NUM_CELLS_SERIES;
            vsum += ocv + pack_currents[i] * r;
        }
        array->bus_voltage = vsum / num_conn;
    }
}

int corvus_array_find_pack_index(const corvus_array_t *array, int pack_id)
{
    for (int i = 0; i < array->num_packs; i++) {
        if (array->controllers[i].pack.pack_id == pack_id)
            return i;
    }
    return -1;
}

void corvus_array_step(corvus_array_t *array, double dt,
                       double requested_current,
                       const double *external_heat)
{
    /* 1. Step all controllers (alarms, limits, mode transitions) */
    for (int i = 0; i < array->num_packs; i++)
        corvus_controller_step(&array->controllers[i], dt, array->bus_voltage);

    /* Gather connected pack indices */
    int conn_idx[BMS_MAX_PACKS];
    int num_conn = 0;
    for (int i = 0; i < array->num_packs; i++) {
        if (array->controllers[i].mode == BMS_MODE_CONNECTED)
            conn_idx[num_conn++] = i;
    }

    /* 2. Solve current distribution */
    if (num_conn > 0) {
        double pack_currents[BMS_MAX_PACKS];
        memset(pack_currents, 0, sizeof(pack_currents));

        /* Need limits computed before solving */
        corvus_array_compute_limits(array);

        if (requested_current != 0.0)
            solve_currents(array, conn_idx, num_conn, requested_current, false, pack_currents);
        else
            solve_currents(array, conn_idx, num_conn, 0.0, true, pack_currents);

        /* 3. Step physics for connected packs */
        for (int j = 0; j < num_conn; j++) {
            int idx = conn_idx[j];
            corvus_controller_t *c = &array->controllers[idx];
            double ext_h = external_heat ? external_heat[idx] : 0.0;
            corvus_pack_step(&c->pack, dt, pack_currents[j],
                             c->contactors_closed, ext_h);
        }
    } else {
        corvus_array_update_bus_voltage(array);
    }

    /* Step physics for non-connected packs (zero current) */
    for (int i = 0; i < array->num_packs; i++) {
        if (array->controllers[i].mode != BMS_MODE_CONNECTED) {
            double ext_h = external_heat ? external_heat[i] : 0.0;
            corvus_pack_step(&array->controllers[i].pack, dt, 0.0,
                             array->controllers[i].contactors_closed, ext_h);
        }
    }

    corvus_array_compute_limits(array);
}
