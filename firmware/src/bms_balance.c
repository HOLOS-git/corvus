/**
 * bms_balance.c — Passive cell balancing via BQ76952
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include "bms_balance.h"
#include "bms_config.h"
#include <string.h>

/* 0.2C in mA = 0.2 * 128000 = 25600 */
#define BALANCE_MAX_CURRENT_MA  25600

void bms_balance_init(bms_balance_state_t *bal)
{
    memset(bal, 0, sizeof(*bal));
}

void bms_balance_run(bms_balance_state_t *bal, const bms_pack_data_t *pack)
{
    uint8_t mod;
    uint8_t cell;
    int32_t abs_current;
    uint16_t imbalance;
    uint16_t balance_target;

    /* Only balance in READY or CONNECTED */
    if (pack->mode != BMS_MODE_READY && pack->mode != BMS_MODE_CONNECTED) {
        /* Disable all balancing */
        if (bal->active) {
            for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
                bal->cell_mask[mod] = 0U;
                bms_hal_bq76952_set_balance(mod, 0U);
            }
            bal->active = false;
        }
        return;
    }

    /* Don't balance during high-current charging */
    abs_current = pack->pack_current_ma;
    if (abs_current < 0) { abs_current = -abs_current; }
    if (abs_current > BALANCE_MAX_CURRENT_MA) {
        if (bal->active) {
            for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
                bal->cell_mask[mod] = 0U;
                bms_hal_bq76952_set_balance(mod, 0U);
            }
            bal->active = false;
        }
        return;
    }

    /* Check imbalance */
    imbalance = pack->max_cell_mv - pack->min_cell_mv;
    if (imbalance <= BMS_BALANCE_THRESHOLD_MV) {
        /* No need to balance */
        if (bal->active) {
            for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
                bal->cell_mask[mod] = 0U;
                bms_hal_bq76952_set_balance(mod, 0U);
            }
            bal->active = false;
        }
        return;
    }

    /* Balance cells above (min_cell + threshold/2) */
    balance_target = pack->min_cell_mv + (BMS_BALANCE_THRESHOLD_MV / 2U);
    bal->active = true;

    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        uint16_t mask = 0U;
        for (cell = 0U; cell < BMS_SE_PER_MODULE; cell++) {
            if (pack->modules[mod].cell_mv[cell] > balance_target) {
                mask |= (uint16_t)(1U << cell);
            }
        }
        bal->cell_mask[mod] = mask;
        bms_hal_bq76952_set_balance(mod, mask);
    }
}

/* ── HAL implementation (mock for desktop, real for STM32) ─────────── */
/* Weak definition — overridden by hal_mock.c or hal_stm32f4.c */

#ifdef DESKTOP_BUILD
/* Static storage for mock balance state */
static uint16_t s_mock_balance_mask[BMS_NUM_MODULES];

void bms_hal_bq76952_set_balance(uint8_t module_id, uint16_t cell_mask)
{
    if (module_id < BMS_NUM_MODULES) {
        s_mock_balance_mask[module_id] = cell_mask;
    }
}

uint16_t mock_get_balance_mask(uint8_t module_id)
{
    if (module_id < BMS_NUM_MODULES) {
        return s_mock_balance_mask[module_id];
    }
    return 0U;
}
#endif
