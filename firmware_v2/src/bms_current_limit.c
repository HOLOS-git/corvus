/**
 * @file bms_current_limit.c
 * @brief Temperature/SoC/SEV current derating (§7.4)
 *
 * Street Smart Edition.
 * P0-05: Sub-zero charge limit is 0A (not 5A via OC formula margin).
 * The actual hard fault is in bms_protection.c; this function returns
 * 0A charge limit below 5°C which is the correct derating curve.
 */

#include "bms_current_limit.h"
#include "bms_config.h"

static int32_t interp_i32(const int32_t *x_bp, const int32_t *y_bp,
                           uint8_t n, int32_t x)
{
    uint8_t i;
    if (x <= x_bp[0]) { return y_bp[0]; }
    if (x >= x_bp[n - 1U]) { return y_bp[n - 1U]; }

    for (i = 1U; i < n; i++) {
        if (x <= x_bp[i]) {
            int32_t dx = x_bp[i] - x_bp[i - 1U];
            int32_t dy = y_bp[i] - y_bp[i - 1U];
            if (dx == 0) { return y_bp[i]; }
            return y_bp[i - 1U] + (dy * (x - x_bp[i - 1U])) / dx;
        }
    }
    return y_bp[n - 1U];
}

static int32_t centi_c_to_ma(int32_t centi_c)
{
    return (int32_t)(((int64_t)centi_c * BMS_NOMINAL_CAPACITY_MAH) / 100);
}

/* Temperature derating breakpoints */
static const int32_t temp_chg_bp[] = { -250, 0, 50, 150, 350, 450, 550, 650 };
static const int32_t temp_chg_cr[] = {    0, 0,  0, 300, 300, 200,   0,   0 };
#define TEMP_CHG_N 8U

static const int32_t temp_dchg_bp[] = {
    -250, -150, -100, -50, 0, 50, 100, 250, 300, 350, 450, 550, 600, 650, 700
};
static const int32_t temp_dchg_cr[] = {
    20, 20, 100, 150, 200, 450, 500, 500, 450, 400, 380, 380, 20, 20, 0
};
#define TEMP_DCHG_N 15U

/* SoC derating */
static const int32_t soc_chg_bp[] = {    0, 8500, 9000, 9500, 10000 };
static const int32_t soc_chg_cr[] = { 300,  300,  200,  100,    50 };
#define SOC_CHG_N 5U

static const int32_t soc_dchg_bp[] = {   0, 200,  500,  800, 1000, 1500, 2000, 5000, 10000 };
static const int32_t soc_dchg_cr[] = { 100, 100,  220,  220,  400,  400,  500,  500,   500 };
#define SOC_DCHG_N 9U

/* SEV derating */
static const int32_t sev_chg_bp[] = { 3000, 4100, 4200 };
static const int32_t sev_chg_cr[] = {  300,  300,    0 };
#define SEV_CHG_N 3U

static const int32_t sev_dchg_bp[] = { 3000, 3200, 3300, 3400, 3450, 3550, 4200 };
static const int32_t sev_dchg_cr[] = {    0,    0,  200,  250,  380,  500,  500 };
#define SEV_DCHG_N 7U

static int32_t min32(int32_t a, int32_t b) { return (a < b) ? a : b; }

void bms_current_limit_compute(const bms_pack_data_t *pack,
                                int32_t *max_charge_ma,
                                int32_t *max_discharge_ma)
{
    int32_t tc, td, sc, sd, vc, vd;
    int32_t t = (int32_t)pack->max_temp_deci_c;

    tc = centi_c_to_ma(interp_i32(temp_chg_bp, temp_chg_cr, TEMP_CHG_N, t));
    td = centi_c_to_ma(interp_i32(temp_dchg_bp, temp_dchg_cr, TEMP_DCHG_N, t));

    int32_t s = (int32_t)pack->soc_hundredths;
    sc = centi_c_to_ma(interp_i32(soc_chg_bp, soc_chg_cr, SOC_CHG_N, s));
    sd = centi_c_to_ma(interp_i32(soc_dchg_bp, soc_dchg_cr, SOC_DCHG_N, s));

    {
        int32_t dummy;
        vc = centi_c_to_ma(interp_i32(sev_chg_bp, sev_chg_cr, SEV_CHG_N, (int32_t)pack->max_cell_mv));
        vd = centi_c_to_ma(interp_i32(sev_dchg_bp, sev_dchg_cr, SEV_DCHG_N, (int32_t)pack->min_cell_mv));
        (void)dummy;
    }

    *max_charge_ma = min32(tc, min32(sc, vc));
    *max_discharge_ma = min32(td, min32(sd, vd));

    if (*max_charge_ma < 0) { *max_charge_ma = 0; }
    if (*max_discharge_ma < 0) { *max_discharge_ma = 0; }
}
