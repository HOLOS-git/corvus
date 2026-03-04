/**
 * @file bms_balance.c
 * @brief Passive cell balancing via BQ76952
 *
 * Street Smart Edition. Functionally identical to original.
 */

#include "bms_balance.h"
#include "bms_hal.h"
#include "bms_config.h"
#include <string.h>

#define BALANCE_MAX_CURRENT_MA  25600  /* 0.2C */

void bms_balance_init(bms_balance_state_t *bal)
{
    memset(bal, 0, sizeof(*bal));
}

static void disable_all(bms_balance_state_t *bal)
{
    uint8_t mod;
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        bal->cell_mask[mod] = 0U;
        bms_hal_bq76952_set_balance(mod, 0U);
    }
    bal->active = false;
}

void bms_balance_run(bms_balance_state_t *bal, const bms_pack_data_t *pack)
{
    if (pack->mode != BMS_MODE_READY && pack->mode != BMS_MODE_CONNECTED) {
        if (bal->active) { disable_all(bal); }
        return;
    }

    int32_t abs_current = pack->pack_current_ma;
    if (abs_current < 0) { abs_current = -abs_current; }
    if (abs_current > BALANCE_MAX_CURRENT_MA) {
        if (bal->active) { disable_all(bal); }
        return;
    }

    uint16_t imbalance = pack->max_cell_mv - pack->min_cell_mv;
    if (imbalance <= BMS_BALANCE_THRESHOLD_MV) {
        if (bal->active) { disable_all(bal); }
        return;
    }

    uint16_t target = pack->min_cell_mv + (BMS_BALANCE_THRESHOLD_MV / 2U);
    bal->active = true;

    uint8_t mod, cell;
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        uint16_t mask = 0U;
        for (cell = 0U; cell < BMS_SE_PER_MODULE; cell++) {
            if (pack->modules[mod].cell_mv[cell] > target) {
                mask |= (uint16_t)(1U << cell);
            }
        }
        bal->cell_mask[mod] = mask;
        bms_hal_bq76952_set_balance(mod, mask);
    }
}
