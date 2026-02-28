/**
 * bms_monitor.c — Periodic cell/temperature reading and fault aggregation
 *
 * 10ms monitoring cycle:
 * 1. Read all BQ76952 ASICs (22 modules × 14 cells + 3 temps each)
 * 2. Aggregate: min/max/avg cell voltage, pack voltage, temperatures
 * 3. Check cell imbalance (max - min > threshold)
 * 4. Check BQ76952 hardware safety status registers
 *
 * Reference: Orca ESS Integrator Manual §7.4, Table 17
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include "bms_monitor.h"
#include "bms_bq76952.h"
#include "bms_hal.h"
#include "bms_config.h"

void bms_monitor_init(bms_pack_data_t *pack)
{
    uint16_t i;
    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        pack->cell_mv[i] = 0U;
    }
    pack->pack_voltage_mv = 0U;
    pack->max_cell_mv = 0U;
    pack->min_cell_mv = 0xFFFFU;
    pack->avg_cell_mv = 0U;
    pack->max_temp_deci_c = -400; /* -40.0°C */
    pack->min_temp_deci_c = 7000; /* 700.0°C */
    pack->soc_hundredths = 5000U; /* 50.00% default */
}

void bms_monitor_read_modules(bms_pack_data_t *pack)
{
    uint8_t mod;
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        bms_module_data_t *m = &pack->modules[mod];
        uint8_t cell;
        uint8_t sens;

        /* Read all cell voltages for this module */
        int32_t rc = bq76952_read_all_cells(mod, m->cell_mv);
        m->comm_ok = (rc == 0);

        if (!m->comm_ok) {
            /* Mark communication fault */
            pack->faults.comm_loss = 1U;
            continue;
        }

        /* Copy into flat pack array */
        for (cell = 0U; cell < BMS_SE_PER_MODULE; cell++) {
            uint16_t idx = (uint16_t)((uint16_t)mod * BMS_SE_PER_MODULE + cell);
            pack->cell_mv[idx] = m->cell_mv[cell];
        }

        /* Read stack voltage */
        m->stack_mv = bq76952_read_stack_voltage(mod);

        /* Read temperatures */
        for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
            m->temp_deci_c[sens] = bq76952_read_temperature(mod, sens);
        }

        /* Read BQ76952 safety registers */
        bq76952_read_safety(mod, &m->bq_safety);

        /* Check BQ76952 HW safety flags — independent path */
        if (m->bq_safety.safety_status_a & BQ_SSA_CELL_OV) {
            pack->faults.hw_ov = 1U;
        }
        if (m->bq_safety.safety_status_a & BQ_SSA_CELL_UV) {
            pack->faults.hw_uv = 1U;
        }
        if (m->bq_safety.safety_status_a & BQ_SSA_SC_DCHG) {
            pack->faults.sc_discharge = 1U;
        }
        if (m->bq_safety.safety_status_b & (BQ_SSB_OTD | BQ_SSB_OTC | BQ_SSB_OTF)) {
            pack->faults.hw_ot = 1U;
        }
    }
}

void bms_monitor_aggregate(bms_pack_data_t *pack)
{
    uint16_t i;
    uint32_t sum_mv = 0U;
    uint16_t max_mv = 0U;
    uint16_t min_mv = 0xFFFFU;
    int16_t  max_temp = -400;
    int16_t  min_temp = 7000;
    uint8_t mod, sens;

    /* Aggregate cell voltages */
    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        uint16_t v = pack->cell_mv[i];
        sum_mv += (uint32_t)v;
        if (v > max_mv) { max_mv = v; }
        if (v < min_mv) { min_mv = v; }
    }

    pack->max_cell_mv = max_mv;
    pack->min_cell_mv = min_mv;
    pack->avg_cell_mv = (uint16_t)(sum_mv / (uint32_t)BMS_SE_PER_PACK);
    pack->pack_voltage_mv = sum_mv;

    /* Aggregate temperatures */
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
            int16_t t = pack->modules[mod].temp_deci_c[sens];
            if (t > max_temp) { max_temp = t; }
            if (t < min_temp) { min_temp = t; }
        }
    }
    pack->max_temp_deci_c = max_temp;
    pack->min_temp_deci_c = min_temp;

    /* Cell imbalance check */
    if ((uint16_t)(max_mv - min_mv) > BMS_IMBALANCE_WARN_MV) {
        pack->faults.imbalance = 1U;
        pack->has_warning = true;
    } else {
        pack->faults.imbalance = 0U;
    }
}

void bms_monitor_run(bms_pack_data_t *pack)
{
    bms_monitor_read_modules(pack);
    bms_monitor_aggregate(pack);
    pack->uptime_ms += BMS_MONITOR_PERIOD_MS;
}
