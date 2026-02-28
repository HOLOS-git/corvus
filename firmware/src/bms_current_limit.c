/**
 * bms_current_limit.c — Temperature/SoC/SEV current derating (§7.4)
 *
 * Ported from Python simulation breakpoints (Figures 28, 29, 30).
 * All interpolation is integer-only using fixed-point arithmetic.
 *
 * C-rate breakpoints stored as centi-C (1/100 of C-rate).
 * Final current = centi_c * capacity_mah / 100.
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include "bms_current_limit.h"
#include "bms_config.h"

/* ── Fixed-point linear interpolation ──────────────────────────────── */

/**
 * Linearly interpolate between breakpoint tables.
 * x_bp: breakpoint X values (sorted ascending)
 * y_bp: breakpoint Y values (centi-C × 100, i.e. 300 = 3.0C)
 * n:    number of breakpoints
 * x:    input value (same units as x_bp)
 * Returns interpolated Y value in same units as y_bp.
 */
static int32_t interp_i32(const int32_t *x_bp, const int32_t *y_bp,
                           uint8_t n, int32_t x)
{
    uint8_t i;
    int32_t dx, dy, frac;

    /* Clamp to range */
    if (x <= x_bp[0]) { return y_bp[0]; }
    if (x >= x_bp[n - 1U]) { return y_bp[n - 1U]; }

    /* Find bracketing segment */
    for (i = 1U; i < n; i++) {
        if (x <= x_bp[i]) {
            dx = x_bp[i] - x_bp[i - 1U];
            dy = y_bp[i] - y_bp[i - 1U];
            if (dx == 0) { return y_bp[i]; }
            frac = x - x_bp[i - 1U];
            /* result = y[i-1] + (dy * frac) / dx */
            return y_bp[i - 1U] + (dy * frac) / dx;
        }
    }
    return y_bp[n - 1U];
}

/* Convert centi-C rate to milliamps: centi_c * capacity_mah / 100 */
static int32_t centi_c_to_ma(int32_t centi_c)
{
    return (int32_t)(((int64_t)centi_c * BMS_NOMINAL_CAPACITY_MAH) / 100);
}

/* ══════════════════════════════════════════════════════════════════════
 * Figure 28: Temperature-based current limit (§7.4.1)
 * Breakpoints in deci-°C, C-rates in centi-C (300 = 3.0C)
 * ══════════════════════════════════════════════════════════════════════ */

/* Charge: -25°C→0C, 0→0C, 5→0C, 15→3C, 35→3C, 45→2C, 55→0C, 65→0C */
static const int32_t temp_chg_bp[] = { -250, 0, 50, 150, 350, 450, 550, 650 };
static const int32_t temp_chg_cr[] = {    0, 0,  0, 300, 300, 200,   0,   0 };
#define TEMP_CHG_N 8U

/* Discharge: 15 breakpoints */
static const int32_t temp_dchg_bp[] = {
    -250, -150, -100, -50, 0, 50, 100, 250, 300, 350, 450, 550, 600, 650, 700
};
static const int32_t temp_dchg_cr[] = {
    20, 20, 100, 150, 200, 450, 500, 500, 450, 400, 380, 380, 20, 20, 0
};
#define TEMP_DCHG_N 15U

static void temp_limit(int16_t temp_deci_c,
                       int32_t *charge_ma, int32_t *discharge_ma)
{
    int32_t t = (int32_t)temp_deci_c;
    int32_t cc = interp_i32(temp_chg_bp, temp_chg_cr, TEMP_CHG_N, t);
    int32_t dc = interp_i32(temp_dchg_bp, temp_dchg_cr, TEMP_DCHG_N, t);
    *charge_ma = centi_c_to_ma(cc);
    *discharge_ma = centi_c_to_ma(dc);
}

/* ══════════════════════════════════════════════════════════════════════
 * Figure 29: SoC-based current limit (§7.4.2)
 * SoC breakpoints in hundredths of percent (0–10000)
 * ══════════════════════════════════════════════════════════════════════ */

/* Charge: SoC 0→3C, 85→3C, 90→2C, 95→1C, 100→0.5C */
static const int32_t soc_chg_bp[] = {    0, 8500, 9000, 9500, 10000 };
static const int32_t soc_chg_cr[] = { 300,  300,  200,  100,    50 };
#define SOC_CHG_N 5U

/* Discharge: SoC 0→1C, 2→1C, 5→2.2C, 8→2.2C, 10→4C, 15→4C, 20→5C, 50→5C, 100→5C */
static const int32_t soc_dchg_bp[] = {   0, 200,  500,  800, 1000, 1500, 2000, 5000, 10000 };
static const int32_t soc_dchg_cr[] = { 100, 100,  220,  220,  400,  400,  500,  500,   500 };
#define SOC_DCHG_N 9U

static void soc_limit(uint16_t soc_hundredths,
                      int32_t *charge_ma, int32_t *discharge_ma)
{
    int32_t s = (int32_t)soc_hundredths;
    int32_t cc = interp_i32(soc_chg_bp, soc_chg_cr, SOC_CHG_N, s);
    int32_t dc = interp_i32(soc_dchg_bp, soc_dchg_cr, SOC_DCHG_N, s);
    *charge_ma = centi_c_to_ma(cc);
    *discharge_ma = centi_c_to_ma(dc);
}

/* ══════════════════════════════════════════════════════════════════════
 * Figure 30: SEV (cell voltage) based current limit (§7.4.3)
 * Voltage breakpoints in millivolts
 * ══════════════════════════════════════════════════════════════════════ */

/* Charge: 3000→3C, 4100→3C, 4200→0C */
static const int32_t sev_chg_bp[] = { 3000, 4100, 4200 };
static const int32_t sev_chg_cr[] = {  300,  300,    0 };
#define SEV_CHG_N 3U

/* Discharge: 3000→0C, 3200→0C, 3300→2C, 3400→2.5C, 3450→3.8C, 3550→5C, 4200→5C */
static const int32_t sev_dchg_bp[] = { 3000, 3200, 3300, 3400, 3450, 3550, 4200 };
static const int32_t sev_dchg_cr[] = {    0,    0,  200,  250,  380,  500,  500 };
#define SEV_DCHG_N 7U

static void sev_limit(uint16_t cell_mv,
                      int32_t *charge_ma, int32_t *discharge_ma)
{
    int32_t v = (int32_t)cell_mv;
    int32_t cc = interp_i32(sev_chg_bp, sev_chg_cr, SEV_CHG_N, v);
    int32_t dc = interp_i32(sev_dchg_bp, sev_dchg_cr, SEV_DCHG_N, v);
    *charge_ma = centi_c_to_ma(cc);
    *discharge_ma = centi_c_to_ma(dc);
}

/* ══════════════════════════════════════════════════════════════════════
 * Public API: min-of-three
 * ══════════════════════════════════════════════════════════════════════ */

static int32_t min32(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

void bms_current_limit_compute(
    const bms_pack_data_t *pack,
    int32_t *max_charge_ma,
    int32_t *max_discharge_ma)
{
    int32_t tc, td, sc, sd, vc, vd;

    /* Temperature derating — use worst-case (max) temp for charge,
       worst-case (max) temp for discharge too (conservative) */
    temp_limit(pack->max_temp_deci_c, &tc, &td);

    /* SoC derating */
    soc_limit(pack->soc_hundredths, &sc, &sd);

    /* SEV derating — use max cell for charge limit, min cell for discharge */
    {
        int32_t sev_chg, sev_dchg, dummy;
        sev_limit(pack->max_cell_mv, &sev_chg, &dummy);
        sev_limit(pack->min_cell_mv, &dummy, &sev_dchg);
        vc = sev_chg;
        vd = sev_dchg;
    }

    /* Take minimum of all three sources */
    *max_charge_ma = min32(tc, min32(sc, vc));
    *max_discharge_ma = min32(td, min32(sd, vd));

    /* Floor at zero */
    if (*max_charge_ma < 0) { *max_charge_ma = 0; }
    if (*max_discharge_ma < 0) { *max_discharge_ma = 0; }
}
