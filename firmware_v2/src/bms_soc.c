/**
 * @file bms_soc.c
 * @brief SoC estimation — coulomb counting + OCV reset
 *
 * Street Smart Edition. Functionally identical to original.
 */

#include "bms_soc.h"
#include "bms_config.h"

static const uint16_t ocv_soc_bp[24] = {
       0,  200,  500,  800, 1000, 1500, 2000, 2500,
    3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500,
    7000, 7500, 8000, 8500, 9000, 9500, 9800, 10000
};

static const uint16_t ocv_mv_bp[24] = {
    3000, 3280, 3420, 3480, 3510, 3555, 3590, 3610,
    3625, 3638, 3650, 3662, 3675, 3690, 3710, 3735,
    3765, 3800, 3845, 3900, 3960, 4030, 4100, 4190
};

static uint16_t s_soc_hundredths;
static uint32_t s_low_current_ms;

#define SOC_LOW_CURRENT_MA   2000
#define SOC_OCV_RESET_MS    30000U

void bms_soc_init(uint16_t initial_soc_hundredths)
{
    s_soc_hundredths = initial_soc_hundredths;
    s_low_current_ms = 0U;
}

uint16_t bms_soc_get(void) { return s_soc_hundredths; }

uint16_t bms_soc_from_ocv(uint16_t cell_mv)
{
    uint8_t i;
    if (cell_mv <= ocv_mv_bp[0]) { return ocv_soc_bp[0]; }
    if (cell_mv >= ocv_mv_bp[23]) { return ocv_soc_bp[23]; }

    for (i = 1U; i < 24U; i++) {
        if (cell_mv <= ocv_mv_bp[i]) {
            int32_t dx = (int32_t)ocv_mv_bp[i] - (int32_t)ocv_mv_bp[i - 1U];
            int32_t dy = (int32_t)ocv_soc_bp[i] - (int32_t)ocv_soc_bp[i - 1U];
            if (dx == 0) { return ocv_soc_bp[i]; }
            int32_t frac = (int32_t)cell_mv - (int32_t)ocv_mv_bp[i - 1U];
            return (uint16_t)((int32_t)ocv_soc_bp[i - 1U] + (dy * frac) / dx);
        }
    }
    return ocv_soc_bp[23];
}

void bms_soc_update(bms_pack_data_t *pack, uint32_t dt_ms)
{
    int64_t delta = ((int64_t)pack->pack_current_ma * (int64_t)dt_ms);
    if (pack->pack_current_ma > 0) {
        delta = delta * BMS_COULOMBIC_EFFICIENCY_PPT / 1000;
    }
    delta = delta / ((int64_t)BMS_NOMINAL_CAPACITY_MAH * 360);

    int32_t new_soc = (int32_t)s_soc_hundredths + (int32_t)delta;
    if (new_soc < 0) { new_soc = 0; }
    if (new_soc > 10000) { new_soc = 10000; }
    s_soc_hundredths = (uint16_t)new_soc;

    int32_t abs_current = pack->pack_current_ma;
    if (abs_current < 0) { abs_current = -abs_current; }

    if (abs_current < SOC_LOW_CURRENT_MA) {
        if (s_low_current_ms <= (0xFFFFFFFFU - dt_ms)) {
            s_low_current_ms += dt_ms;
        }
    } else {
        s_low_current_ms = 0U;
    }

    if (s_low_current_ms >= SOC_OCV_RESET_MS && pack->mode == BMS_MODE_READY) {
        s_soc_hundredths = bms_soc_from_ocv(pack->avg_cell_mv);
        s_low_current_ms = 0U;
    }

    pack->soc_hundredths = s_soc_hundredths;
}
