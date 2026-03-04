/**
 * @file bms_monitor.c
 * @brief Cell/temperature monitoring with sensor fault detection
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P0-01: Temperature sensor failure detection (Dave, Priya, Catherine, Yara)
 *     - Sentinel value detection (INT16_MIN from BQ76952 driver)
 *     - Plausibility bounds (-40°C to 120°C)
 *     - Cross-check adjacent sensors (>20°C delta = suspicious)
 *     - 3 consecutive bad scans → sensor_fault = true → fault_latched
 *   P0-02: I2C comm loss → fault_latched after 3 consecutive failures (Dave, Catherine)
 *     - Bus recovery attempted before declaring failure
 *   P2-07: Stack voltage vs sum-of-cells cross-check (Dave, Yara, Priya)
 *     - |sum(cells) - stack_mv| > 2% → plausibility flag
 */

#include "bms_monitor.h"
#include "bms_bq76952.h"
#include "bms_hal.h"
#include "bms_config.h"
#include "bms_soc.h"
#include "bms_current_limit.h"
#include "bms_balance.h"
#include <string.h>

static bms_balance_state_t s_balance;
static uint8_t  s_current_module;
static bool     s_scan_complete;
static uint32_t s_scan_count;

void bms_monitor_init(bms_pack_data_t *pack)
{
    uint16_t i;
    uint8_t mod, sens;

    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        pack->cell_mv[i] = 0U;
    }
    pack->pack_voltage_mv = 0U;
    pack->max_cell_mv = 0U;
    pack->min_cell_mv = 0xFFFFU;
    pack->avg_cell_mv = 0U;
    pack->max_temp_deci_c = -400;
    pack->min_temp_deci_c = 7000;
    pack->soc_hundredths = 5000U;

    /* P0-01: Initialize sensor fault tracking */
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        pack->modules[mod].i2c_fail_count = 0U;
        pack->modules[mod].comm_ok = false;
        for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
            pack->modules[mod].sensor_fault[sens].consec_fault_count = 0U;
            pack->modules[mod].sensor_fault[sens].faulted = false;
            pack->modules[mod].sensor_fault[sens].last_valid_deci_c = 250; /* 25°C default */
        }
    }

    s_current_module = 0U;
    s_scan_complete = false;
    s_scan_count = 0U;

    bms_soc_init(pack->soc_hundredths);
    bms_balance_init(&s_balance);
}

/**
 * P0-01: Check if a temperature reading is plausible.
 *
 * Safety rationale: A sensor returning exactly 0°C on 3+ scans is likely
 * failed (corroded wire reads as low resistance → 0°C after conversion).
 * Readings outside -40°C..120°C are physically impossible for NMC packs.
 */
static bool temp_is_plausible(int16_t temp_deci_c)
{
    if (temp_deci_c == BMS_TEMP_SENSOR_SENTINEL) {
        return false;  /* I2C read failed */
    }
    if (temp_deci_c < BMS_TEMP_PLAUSIBLE_MIN_DC ||
        temp_deci_c > BMS_TEMP_PLAUSIBLE_MAX_DC) {
        return false;  /* physically impossible */
    }
    return true;
}

/**
 * P0-01: Process a single temperature sensor reading.
 * Tracks consecutive faults, cross-checks, updates fault state.
 */
static void process_temp_sensor(bms_module_data_t *mod,
                                uint8_t sensor_idx,
                                int16_t raw_temp,
                                bms_pack_data_t *pack)
{
    bms_sensor_fault_t *sf = &mod->sensor_fault[sensor_idx];

    if (!temp_is_plausible(raw_temp)) {
        sf->consec_fault_count++;
        if (sf->consec_fault_count >= BMS_TEMP_FAULT_CONSEC_SCANS) {
            sf->faulted = true;
            pack->faults.sensor_fault = 1U;
            pack->fault_latched = true;  /* P0-01: sensor fault latches */
            BMS_LOG("P0-01: Sensor fault latched — module sensor %u", sensor_idx);
        }
        /* Use last valid reading for aggregation (conservative) */
        mod->temp_deci_c[sensor_idx] = sf->last_valid_deci_c;
    } else {
        /* P0-01: Check for suspicious exact-zero */
        if (raw_temp == BMS_TEMP_ZERO_EXACT_DC) {
            sf->consec_fault_count++;
            if (sf->consec_fault_count >= BMS_TEMP_FAULT_CONSEC_SCANS) {
                sf->faulted = true;
                pack->faults.sensor_fault = 1U;
                pack->fault_latched = true;
            }
            mod->temp_deci_c[sensor_idx] = sf->last_valid_deci_c;
        } else {
            sf->consec_fault_count = 0U;
            sf->last_valid_deci_c = raw_temp;
            mod->temp_deci_c[sensor_idx] = raw_temp;
        }
    }
}

/**
 * P0-01: Cross-check adjacent sensors within a module.
 * If one sensor reads 25°C and adjacent reads very different → flag.
 */
static void cross_check_module_temps(bms_module_data_t *mod,
                                     bms_pack_data_t *pack)
{
    uint8_t s;
    for (s = 0U; s < BMS_TEMPS_PER_MODULE; s++) {
        if (mod->sensor_fault[s].faulted) { continue; }

        uint8_t other = (s + 1U) % BMS_TEMPS_PER_MODULE;
        if (mod->sensor_fault[other].faulted) { continue; }

        int16_t delta = mod->temp_deci_c[s] - mod->temp_deci_c[other];
        if (delta < 0) { delta = -delta; }

        if (delta > BMS_TEMP_ADJACENT_DELTA_DC) {
            /* Suspicious — one sensor may be failing */
            BMS_LOG("P0-01: Cross-check fail — sensor %u=%d, sensor %u=%d (delta=%d)",
                    s, mod->temp_deci_c[s], other, mod->temp_deci_c[other], delta);
            pack->has_warning = true;
        }
    }
}

void bms_monitor_read_module(bms_pack_data_t *pack, uint8_t mod_idx)
{
    bms_module_data_t *m = &pack->modules[mod_idx];
    uint8_t cell, sens;

    /* Read all cell voltages */
    int32_t rc = bq76952_read_all_cells(mod_idx, m->cell_mv);

    if (rc != 0) {
        /* P0-02: I2C failure tracking with bus recovery */
        m->i2c_fail_count++;

        if (m->i2c_fail_count == 1U) {
            /* First failure: attempt bus recovery */
            hal_i2c_bus_recovery();
        }

        if (m->i2c_fail_count >= BMS_I2C_FAULT_CONSEC_COUNT) {
            /* P0-02: 3 consecutive failures → LATCH FAULT (Dave, Catherine)
             * Original code set comm_loss but NEVER set fault_latched.
             * Pack stayed CONNECTED with 14 unmonitored cells. */
            m->comm_ok = false;
            pack->faults.comm_loss = 1U;
            pack->fault_latched = true;
            BMS_LOG("P0-02: comm_loss LATCHED — module %u, %u consecutive failures",
                    mod_idx, m->i2c_fail_count);
        }
        return;
    }

    /* Successful read — reset failure counter */
    m->comm_ok = true;
    m->i2c_fail_count = 0U;

    /* Copy into flat pack array */
    for (cell = 0U; cell < BMS_SE_PER_MODULE; cell++) {
        uint16_t idx = (uint16_t)((uint16_t)mod_idx * BMS_SE_PER_MODULE + cell);
        pack->cell_mv[idx] = m->cell_mv[cell];
    }

    /* Read stack voltage */
    m->stack_mv = bq76952_read_stack_voltage(mod_idx);

    /* P2-07: Stack vs cells cross-check (Dave, Yara, Priya)
     * |sum(cells) - stack_mv| > 2% → anomaly flag */
    {
        uint32_t sum_cells = 0U;
        for (cell = 0U; cell < BMS_SE_PER_MODULE; cell++) {
            sum_cells += (uint32_t)m->cell_mv[cell];
        }
        if (m->stack_mv > 0U) {
            uint32_t diff;
            if (sum_cells > (uint32_t)m->stack_mv) {
                diff = sum_cells - (uint32_t)m->stack_mv;
            } else {
                diff = (uint32_t)m->stack_mv - sum_cells;
            }
            /* 2% threshold */
            uint32_t threshold = ((uint32_t)m->stack_mv * BMS_STACK_VS_CELLS_PCT) / 100U;
            if (diff > threshold) {
                pack->faults.plausibility = 1U;
                BMS_LOG("P2-07: Stack vs cells mismatch — module %u, sum=%u, stack=%u",
                        mod_idx, (unsigned)sum_cells, m->stack_mv);
            }
        }
    }

    /* P2-07: dV/dt rate-of-change plausibility check
     * Flag if any cell voltage changes faster than BMS_CELL_DV_DT_MAX_MV
     * per scan period (BMS_MONITOR_PERIOD_MS). Physically impossible rate
     * indicates I2C data corruption or sensor failure. */
    {
        for (cell = 0U; cell < BMS_SE_PER_MODULE; cell++) {
            uint16_t prev = m->prev_cell_mv[cell];
            uint16_t curr = m->cell_mv[cell];
            if (prev > 0U && curr > 0U) {
                uint16_t dv = (curr > prev) ? (curr - prev) : (prev - curr);
                if (dv > BMS_CELL_DV_DT_MAX_MV) {
                    pack->faults.plausibility = 1U;
                    BMS_LOG("P2-07: dV/dt exceeded — module %u cell %u, dV=%umV (max=%u)",
                            mod_idx, cell, dv, BMS_CELL_DV_DT_MAX_MV);
                }
            }
            m->prev_cell_mv[cell] = curr;
        }
    }

    /* Read temperatures with P0-01 sensor fault detection */
    for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
        int16_t raw_temp = bq76952_read_temperature(mod_idx, sens);
        process_temp_sensor(m, sens, raw_temp, pack);
    }

    /* P0-01: Cross-check adjacent sensors */
    cross_check_module_temps(m, pack);

    /* Read BQ76952 safety registers */
    (void)bq76952_read_safety(mod_idx, &m->bq_safety);

    /* Check BQ76952 HW safety flags */
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

void bms_monitor_aggregate(bms_pack_data_t *pack)
{
    uint16_t i;
    uint32_t sum_mv = 0U;
    uint16_t max_mv = 0U;
    uint16_t min_mv = 0xFFFFU;
    int16_t  max_temp = -400;
    int16_t  min_temp = 7000;
    uint8_t  mod, sens;

    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        uint16_t v = pack->cell_mv[i];
        sum_mv += (uint32_t)v;
        if (v > max_mv) { max_mv = v; }
        if (v > 0U && v < min_mv) { min_mv = v; }
    }

    pack->max_cell_mv = max_mv;
    pack->min_cell_mv = min_mv;
    pack->avg_cell_mv = (uint16_t)(sum_mv / (uint32_t)BMS_SE_PER_PACK);
    pack->pack_voltage_mv = sum_mv;

    /* Aggregate temperatures — only from non-faulted sensors (P0-01) */
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
            if (pack->modules[mod].sensor_fault[sens].faulted) {
                continue;  /* P0-01: skip faulted sensors */
            }
            int16_t t = pack->modules[mod].temp_deci_c[sens];
            if (t > max_temp) { max_temp = t; }
            if (t < min_temp) { min_temp = t; }
        }
    }
    pack->max_temp_deci_c = max_temp;
    pack->min_temp_deci_c = min_temp;

    /* P2-07: Inter-module temperature comparison
     * Flag if one module's average temp is >BMS_INTER_MODULE_TEMP_DELTA_DC
     * different from its neighbors — indicates sensor fault or localized issue. */
    {
        int16_t mod_avg_temp[BMS_NUM_MODULES];
        uint8_t m2;

        /* Compute per-module average temperature (non-faulted sensors only) */
        for (m2 = 0U; m2 < BMS_NUM_MODULES; m2++) {
            int32_t sum_t = 0;
            uint8_t count = 0U;
            uint8_t s2;
            for (s2 = 0U; s2 < BMS_TEMPS_PER_MODULE; s2++) {
                if (!pack->modules[m2].sensor_fault[s2].faulted) {
                    sum_t += (int32_t)pack->modules[m2].temp_deci_c[s2];
                    count++;
                }
            }
            mod_avg_temp[m2] = (count > 0U) ? (int16_t)(sum_t / (int32_t)count) : 0;
        }

        /* Compare adjacent modules */
        for (m2 = 1U; m2 < BMS_NUM_MODULES; m2++) {
            int16_t delta = mod_avg_temp[m2] - mod_avg_temp[m2 - 1U];
            if (delta < 0) { delta = -delta; }
            if (delta > BMS_INTER_MODULE_TEMP_DELTA_DC) {
                pack->faults.plausibility = 1U;
                pack->has_warning = true;
                BMS_LOG("P2-07: Inter-module temp delta — mod %u=%d, mod %u=%d (delta=%d)",
                        m2 - 1U, mod_avg_temp[m2 - 1U], m2, mod_avg_temp[m2], delta);
            }
        }
    }

    /* Cell imbalance */
    if (max_mv > 0U && min_mv < 0xFFFFU &&
        (uint16_t)(max_mv - min_mv) > BMS_IMBALANCE_WARN_MV) {
        pack->faults.imbalance = 1U;
        pack->has_warning = true;
    } else {
        pack->faults.imbalance = 0U;
    }

    /* P0-03: Read actual bus voltage for pre-charge reference */
    /* P3-04: Use single calibration constant (Dave — ADC scaling consistency) */
    pack->bus_voltage_mv = (uint32_t)hal_adc_read(ADC_BUS_VOLTAGE)
                         * BMS_ADC_BUS_VOLTAGE_SCALE_NUM / BMS_ADC_BUS_VOLTAGE_SCALE_DEN;
}

void bms_monitor_run(bms_pack_data_t *pack)
{
    s_scan_complete = false;

    bms_monitor_read_module(pack, s_current_module);
    s_current_module++;

    if (s_current_module >= BMS_NUM_MODULES) {
        s_current_module = 0U;
        s_scan_complete = true;
        s_scan_count++;
        bms_monitor_aggregate(pack);
    }

    bms_soc_update(pack, BMS_MONITOR_PERIOD_MS);
    bms_current_limit_compute(pack, &pack->charge_limit_ma, &pack->discharge_limit_ma);
    bms_balance_run(&s_balance, pack);

    pack->uptime_ms += BMS_MONITOR_PERIOD_MS;
}

uint8_t  bms_monitor_get_scan_index(void) { return s_current_module; }
bool     bms_monitor_scan_complete(void)   { return s_scan_complete; }
uint32_t bms_monitor_get_scan_count(void)  { return s_scan_count; }
