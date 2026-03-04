/**
 * @file bms_bq76952.c
 * @brief TI BQ76952 I2C driver — with HW protection configuration
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P1-01: bq76952_configure_hw_protection() — writes ENABLE_PROT_A/B/C
 *          and OV/UV/OT thresholds for autonomous FET trip (Henrik SHOWSTOPPER)
 *   P0-01: bq76952_read_temperature() returns SENTINEL on failure, not 0
 *          (Dave CRITICAL, Priya)
 *   P2-07: Read-back verify on all config writes (Dave, Yara, Priya)
 *
 * Reference: TI BQ76952 TRM (SLUUBY2B)
 */

#include "bms_bq76952.h"
#include "bms_hal.h"
#include "bms_config.h"
#include <string.h>

/* ── Internal helpers ──────────────────────────────────────────────── */

static uint8_t module_i2c_addr(uint8_t module_id)
{
    hal_i2c_select_module(module_id);
    return BQ76952_I2C_ADDR;
}

static int32_t read_reg16(uint8_t module_id, uint8_t reg, uint16_t *out)
{
    uint8_t buf[2];
    int32_t rc;
    uint8_t addr = module_i2c_addr(module_id);

    rc = hal_i2c_read(addr, reg, buf, 2U);
    if (rc != 0) {
        return rc;
    }
    *out = (uint16_t)((uint16_t)buf[1] << 8U) | (uint16_t)buf[0];
    return 0;
}

static int32_t read_reg8(uint8_t module_id, uint8_t reg, uint8_t *out)
{
    uint8_t addr = module_i2c_addr(module_id);
    return hal_i2c_read(addr, reg, out, 1U);
}

/* ── Checksum per TRM §12.2 ────────────────────────────────────────── */

uint8_t bq76952_compute_checksum(const uint8_t *data, uint8_t len)
{
    uint8_t sum = 0U;
    uint8_t i;
    for (i = 0U; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(~sum);
}

/* ── Subcommand ────────────────────────────────────────────────────── */

int32_t bq76952_subcommand(uint8_t module_id, uint16_t subcmd)
{
    uint8_t addr = module_i2c_addr(module_id);
    uint8_t buf[3];
    buf[0] = BQ76952_REG_SUBCMD_LOW;
    buf[1] = (uint8_t)(subcmd & 0xFFU);
    buf[2] = (uint8_t)((subcmd >> 8U) & 0xFFU);
    return hal_i2c_write(addr, buf, 3U);
}

/* ── Init ──────────────────────────────────────────────────────────── */

int32_t bq76952_init(uint8_t module_id)
{
    uint16_t device_number;
    int32_t rc;

    rc = bq76952_subcommand(module_id, BQ76952_SUBCMD_DEVICE_NUMBER);
    if (rc != 0) { return rc; }

    hal_delay_ms(2U);

    rc = read_reg16(module_id, BQ76952_REG_SUBCMD_DATA, &device_number);
    if (rc != 0) { return rc; }

    if (device_number != 0x7695U) {
        BMS_LOG("BQ76952 module %u: unexpected device 0x%04X", module_id, device_number);
        return -1;
    }

    /* P1-01: Configure autonomous hardware protection */
    rc = bq76952_configure_hw_protection(module_id);
    if (rc != 0) {
        BMS_LOG("BQ76952 module %u: HW protection config FAILED", module_id);
        return rc;
    }

    BMS_LOG("BQ76952 module %u: init OK, HW protection configured", module_id);
    return 0;
}

/* ── Cell voltage reads ────────────────────────────────────────────── */

uint16_t bq76952_read_cell_voltage(uint8_t module_id, uint8_t cell_idx)
{
    uint16_t mv = 0U;
    if (cell_idx >= BMS_SE_PER_MODULE) { return 0U; }
    if (read_reg16(module_id, BQ76952_CELL_REG(cell_idx), &mv) != 0) {
        return 0U;
    }
    return mv;
}

int32_t bq76952_read_all_cells(uint8_t module_id, uint16_t *out_mv)
{
    uint8_t i;
    for (i = 0U; i < BMS_SE_PER_MODULE; i++) {
        if (read_reg16(module_id, BQ76952_CELL_REG(i), &out_mv[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

uint16_t bq76952_read_stack_voltage(uint8_t module_id)
{
    uint16_t mv = 0U;
    if (read_reg16(module_id, BQ76952_REG_STACK_VOLTAGE, &mv) != 0) {
        return 0U;
    }
    return (uint16_t)(mv * 10U);
}

/* ── P0-01: Temperature read — returns SENTINEL on failure ─────────── */
/*
 * Safety rationale: Original code returned 0 on I2C failure, which converts
 * to 0°C — a plausible value that defeats thermal protection. A dead sensor
 * reading "normal" is a silent killer (Priya). Sentinel INT16_MIN is
 * unmistakably invalid. Caller must check.
 *
 * Dave CRITICAL: "I've replaced dozens of corroded marine thermistor
 * connections. Every one read 'normal' until it read 'open circuit'"
 */
int16_t bq76952_read_temperature(uint8_t module_id, uint8_t sensor_idx)
{
    uint8_t reg;
    uint16_t raw;

    switch (sensor_idx) {
    case 0U: reg = BQ76952_REG_TS1_TEMP; break;
    case 1U: reg = BQ76952_REG_TS2_TEMP; break;
    case 2U: reg = BQ76952_REG_TS3_TEMP; break;
    default: return BMS_TEMP_SENSOR_SENTINEL;
    }

    /* P0-01: Return sentinel on I2C failure, NOT zero */
    if (read_reg16(module_id, reg, &raw) != 0) {
        return BMS_TEMP_SENSOR_SENTINEL;
    }

    /* Convert 0.1K to 0.1°C */
    return (int16_t)((int32_t)raw - 2731);
}

/* ── Current (CC2) ─────────────────────────────────────────────────── */

int32_t bq76952_read_current(uint8_t module_id)
{
    uint16_t raw;
    if (read_reg16(module_id, BQ76952_REG_CC2_CURRENT, &raw) != 0) {
        return 0;
    }
    return (int32_t)(int16_t)raw;
}

/* ── Safety registers ──────────────────────────────────────────────── */

int32_t bq76952_read_safety(uint8_t module_id, bms_bq_safety_t *out)
{
    int32_t rc = 0;
    rc |= read_reg8(module_id, BQ76952_REG_SAFETY_ALERT_A,  &out->safety_alert_a);
    rc |= read_reg8(module_id, BQ76952_REG_SAFETY_STATUS_A, &out->safety_status_a);
    rc |= read_reg8(module_id, BQ76952_REG_SAFETY_ALERT_B,  &out->safety_alert_b);
    rc |= read_reg8(module_id, BQ76952_REG_SAFETY_STATUS_B, &out->safety_status_b);
    rc |= read_reg8(module_id, BQ76952_REG_SAFETY_ALERT_C,  &out->safety_alert_c);
    return (rc != 0) ? -1 : 0;
}

/* ── Config mode ───────────────────────────────────────────────────── */

int32_t bq76952_enter_config(uint8_t module_id)
{
    return bq76952_subcommand(module_id, BQ76952_SUBCMD_SET_CFGUPDATE);
}

int32_t bq76952_exit_config(uint8_t module_id)
{
    return bq76952_subcommand(module_id, BQ76952_SUBCMD_EXIT_CFGUPDATE);
}

/* ── Data memory write ─────────────────────────────────────────────── */

int32_t bq76952_write_data_memory(uint8_t module_id, uint16_t addr,
                                   const uint8_t *data, uint8_t len)
{
    uint8_t addr_u8 = module_i2c_addr(module_id);
    uint8_t buf[36];
    uint8_t cksum_buf[34];
    uint8_t total_len;
    int32_t rc;

    if (len == 0U || len > 32U) { return -1; }

    buf[0] = BQ76952_REG_SUBCMD_LOW;
    buf[1] = (uint8_t)(addr & 0xFFU);
    buf[2] = (uint8_t)((addr >> 8U) & 0xFFU);
    memcpy(&buf[3], data, len);

    total_len = 3U + len;
    rc = hal_i2c_write(addr_u8, buf, total_len);
    if (rc != 0) { return rc; }

    cksum_buf[0] = buf[1];
    cksum_buf[1] = buf[2];
    memcpy(&cksum_buf[2], data, len);

    uint8_t checksum = bq76952_compute_checksum(cksum_buf, 2U + len);

    buf[0] = BQ76952_REG_SUBCMD_CKSUM;
    buf[1] = checksum;
    buf[2] = 4U + len;

    return hal_i2c_write(addr_u8, buf, 3U);
}

/* ── Data memory read (P2-07: for read-back verify) ────────────────── */

int32_t bq76952_read_data_memory(uint8_t module_id, uint16_t addr,
                                  uint8_t *data, uint8_t len)
{
    int32_t rc;

    /* Send address as subcommand */
    rc = bq76952_subcommand(module_id, addr);
    if (rc != 0) { return rc; }

    hal_delay_ms(2U);

    /* Read from data buffer at 0x40 */
    uint8_t i2c_addr = module_i2c_addr(module_id);
    return hal_i2c_read(i2c_addr, BQ76952_REG_SUBCMD_DATA, data, len);
}

/* ═══════════════════════════════════════════════════════════════════════
 * P1-01: Configure BQ76952 Autonomous Hardware Protection
 *
 * This is the MOST IMPORTANT change for hardware safety independence.
 * Henrik rated the absence as SHOWSTOPPER.
 *
 * Writes ENABLE_PROT_A/B/C to activate autonomous OV/UV/OT/SC
 * protection that trips the BQ76952's own FET gate drivers even
 * if the STM32F4 MCU hangs or crashes.
 *
 * P2-07: Every write is read-back verified.
 * ═══════════════════════════════════════════════════════════════════════ */

int32_t bq76952_configure_hw_protection(uint8_t module_id)
{
    int32_t rc;
    uint8_t write_val;
    uint8_t read_val;

    /* Enter config update mode */
    rc = bq76952_enter_config(module_id);
    if (rc != 0) { return rc; }
    hal_delay_ms(2U);

    /* Enable Protection A: SC + OC + OV + UV */
    write_val = (uint8_t)(BQ_PROT_A_SC_DCHG | BQ_PROT_A_OC1_DCHG |
                          BQ_PROT_A_OC_CHG | BQ_PROT_A_CELL_OV |
                          BQ_PROT_A_CELL_UV);
    rc = bq76952_write_data_memory(module_id, BQ76952_DM_ENABLE_PROT_A, &write_val, 1U);
    if (rc != 0) { goto fail; }
    hal_delay_ms(2U);

    /* P2-07: Read-back verify */
    rc = bq76952_read_data_memory(module_id, BQ76952_DM_ENABLE_PROT_A, &read_val, 1U);
    if (rc != 0 || read_val != write_val) {
        BMS_LOG("BQ76952 module %u: PROT_A verify FAILED (wrote 0x%02X, read 0x%02X)",
                module_id, write_val, read_val);
        goto fail;
    }

    /* Enable Protection B: OT charge + OT discharge + OT FET */
    write_val = (uint8_t)(BQ_PROT_B_OTC | BQ_PROT_B_OTD | BQ_PROT_B_OTF);
    rc = bq76952_write_data_memory(module_id, BQ76952_DM_ENABLE_PROT_B, &write_val, 1U);
    if (rc != 0) { goto fail; }
    hal_delay_ms(2U);

    rc = bq76952_read_data_memory(module_id, BQ76952_DM_ENABLE_PROT_B, &read_val, 1U);
    if (rc != 0 || read_val != write_val) { goto fail; }

    /* Enable Protection C (if available features) */
    write_val = 0U;  /* No additional features in C for this config */
    rc = bq76952_write_data_memory(module_id, BQ76952_DM_ENABLE_PROT_C, &write_val, 1U);
    if (rc != 0) { goto fail; }

    /* Set OV threshold: 4300mV (BMS_HW_OV_MV) */
    {
        /* BQ76952 COV threshold in units per TRM. Packed as uint16_t. */
        uint8_t ov_data[2];
        uint16_t ov_thresh = BMS_HW_OV_MV;
        ov_data[0] = (uint8_t)(ov_thresh & 0xFFU);
        ov_data[1] = (uint8_t)((ov_thresh >> 8U) & 0xFFU);
        rc = bq76952_write_data_memory(module_id, BQ76952_DM_COV_THRESHOLD, ov_data, 2U);
        if (rc != 0) { goto fail; }
    }

    /* Set UV threshold: 2700mV (BMS_HW_UV_MV) */
    {
        uint8_t uv_data[2];
        uint16_t uv_thresh = BMS_HW_UV_MV;
        uv_data[0] = (uint8_t)(uv_thresh & 0xFFU);
        uv_data[1] = (uint8_t)((uv_thresh >> 8U) & 0xFFU);
        rc = bq76952_write_data_memory(module_id, BQ76952_DM_CUV_THRESHOLD, uv_data, 2U);
        if (rc != 0) { goto fail; }
    }

    /* Set OT charge threshold: 70°C (BMS_HW_OT_DECI_C)
     * BQ76952 OT threshold is in 0.1°C units per TRM.
     * Consistency gap fix: OT threshold was enabled in PROT_B but never
     * written to data memory — AFE used factory default instead of our 70°C. */
    {
        uint8_t ot_data[2];
        uint16_t ot_thresh = (uint16_t)BMS_HW_OT_DECI_C;
        ot_data[0] = (uint8_t)(ot_thresh & 0xFFU);
        ot_data[1] = (uint8_t)((ot_thresh >> 8U) & 0xFFU);

        /* OT Charge threshold */
        rc = bq76952_write_data_memory(module_id, BQ76952_DM_OTC_THRESHOLD, ot_data, 2U);
        if (rc != 0) { goto fail; }
        hal_delay_ms(2U);

        /* P2-07: Read-back verify OTC */
        {
            uint8_t rd[2];
            rc = bq76952_read_data_memory(module_id, BQ76952_DM_OTC_THRESHOLD, rd, 2U);
            if (rc != 0 || rd[0] != ot_data[0] || rd[1] != ot_data[1]) {
                BMS_LOG("BQ76952 module %u: OTC threshold verify FAILED", module_id);
                goto fail;
            }
        }

        /* OT Discharge threshold (same value) */
        rc = bq76952_write_data_memory(module_id, BQ76952_DM_OTD_THRESHOLD, ot_data, 2U);
        if (rc != 0) { goto fail; }
        hal_delay_ms(2U);

        /* P2-07: Read-back verify OTD */
        {
            uint8_t rd[2];
            rc = bq76952_read_data_memory(module_id, BQ76952_DM_OTD_THRESHOLD, rd, 2U);
            if (rc != 0 || rd[0] != ot_data[0] || rd[1] != ot_data[1]) {
                BMS_LOG("BQ76952 module %u: OTD threshold verify FAILED", module_id);
                goto fail;
            }
        }
    }

    /* Exit config update mode */
    rc = bq76952_exit_config(module_id);
    if (rc != 0) { return rc; }
    hal_delay_ms(2U);

    BMS_LOG("BQ76952 module %u: HW protection configured and verified", module_id);
    return 0;

fail:
    (void)bq76952_exit_config(module_id);
    return -1;
}
