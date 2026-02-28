/**
 * bms_bq76952.c — TI BQ76952 I2C driver
 *
 * I2C protocol per TI BQ76952 TRM (SLUUBY2B):
 *   §4.1: Direct commands — 2-byte read/write at register address
 *   §12.2: Data memory access via subcmd 0x3E/0x3F + checksum
 *
 * Each Orca module has one BQ76952 at base address 0x08.
 * Module addressing: the I2C mux selects the module's bus segment,
 * so we compute effective address as BQ76952_I2C_ADDR (all same).
 * The HAL layer handles mux selection via module_id.
 *
 * Cell voltage formula: raw 16-bit value in mV directly (TRM §4.1)
 * Temperature formula: raw in 0.1K → convert to 0.1°C: (raw - 2731)
 *
 * Reference driver: skriachko/bq76952 (MIT, STM32WB55)
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include "bms_bq76952.h"
#include "bms_hal.h"
#include "bms_config.h"
#include <string.h>

/* ── Internal: I2C address for module (all same, mux handles routing) ─ */
static uint8_t module_i2c_addr(uint8_t module_id)
{
    (void)module_id;
    return BQ76952_I2C_ADDR;
}

/* ── Internal: read 2 bytes from direct command register ───────────── */
static int32_t read_reg16(uint8_t module_id, uint8_t reg, uint16_t *out)
{
    uint8_t buf[2];
    int32_t rc;
    uint8_t addr = module_i2c_addr(module_id);

    rc = hal_i2c_read(addr, reg, buf, 2U);
    if (rc != 0) {
        return rc;
    }
    /* BQ76952: LSB first (little-endian) */
    *out = (uint16_t)((uint16_t)buf[1] << 8U) | (uint16_t)buf[0];
    return 0;
}

/* ── Internal: read 1 byte from direct command register ────────────── */
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

/* ── Subcommand: write 2-byte command to 0x3E/0x3F ─────────────────── */
int32_t bq76952_subcommand(uint8_t module_id, uint16_t subcmd)
{
    uint8_t addr = module_i2c_addr(module_id);
    uint8_t buf[3];

    /* Write subcmd low byte to 0x3E, high byte to 0x3F */
    buf[0] = BQ76952_REG_SUBCMD_LOW;
    buf[1] = (uint8_t)(subcmd & 0xFFU);
    buf[2] = (uint8_t)((subcmd >> 8U) & 0xFFU);

    return hal_i2c_write(addr, buf, 3U);
}

/* ── Init: wake device, verify communication ───────────────────────── */
int32_t bq76952_init(uint8_t module_id)
{
    uint16_t device_number;
    int32_t rc;

    /* Send DEVICE_NUMBER subcmd and read back */
    rc = bq76952_subcommand(module_id, BQ76952_SUBCMD_DEVICE_NUMBER);
    if (rc != 0) {
        return rc;
    }

    /* Small delay for subcmd processing */
    hal_delay_ms(2U);

    /* Read response from 0x40 (subcmd data buffer) */
    rc = read_reg16(module_id, BQ76952_REG_SUBCMD_DATA, &device_number);
    if (rc != 0) {
        return rc;
    }

    /* BQ76952 should return 0x7695 */
    if (device_number != 0x7695U) {
        BMS_LOG("BQ76952 module %u: unexpected device 0x%04X", module_id, device_number);
        return -1;
    }

    BMS_LOG("BQ76952 module %u: init OK (device=0x%04X)", module_id, device_number);
    return 0;
}

/* ── Read single cell voltage ──────────────────────────────────────── */
uint16_t bq76952_read_cell_voltage(uint8_t module_id, uint8_t cell_idx)
{
    uint16_t mv = 0U;

    if (cell_idx >= BMS_SE_PER_MODULE) {
        return 0U;
    }

    /* Direct command: cell register = 0x14 + (cell * 2) */
    if (read_reg16(module_id, BQ76952_CELL_REG(cell_idx), &mv) != 0) {
        return 0U;
    }

    return mv;
}

/* ── Read all cells for a module ───────────────────────────────────── */
int32_t bq76952_read_all_cells(uint8_t module_id, uint16_t *out_mv)
{
    uint8_t i;
    for (i = 0U; i < BMS_SE_PER_MODULE; i++) {
        uint16_t mv;
        if (read_reg16(module_id, BQ76952_CELL_REG(i), &mv) != 0) {
            return -1;
        }
        out_mv[i] = mv;
    }
    return 0;
}

/* ── Read stack voltage ────────────────────────────────────────────── */
uint16_t bq76952_read_stack_voltage(uint8_t module_id)
{
    uint16_t mv = 0U;
    /* Stack voltage at 0x34, units: 10mV per LSB → multiply by 10 */
    if (read_reg16(module_id, BQ76952_REG_STACK_VOLTAGE, &mv) != 0) {
        return 0U;
    }
    return (uint16_t)(mv * 10U);
}

/* ── Read temperature ──────────────────────────────────────────────── */
int16_t bq76952_read_temperature(uint8_t module_id, uint8_t sensor_idx)
{
    uint8_t reg;
    uint16_t raw;

    switch (sensor_idx) {
    case 0U: reg = BQ76952_REG_TS1_TEMP; break;
    case 1U: reg = BQ76952_REG_TS2_TEMP; break;
    case 2U: reg = BQ76952_REG_TS3_TEMP; break;
    default: return 0;
    }

    if (read_reg16(module_id, reg, &raw) != 0) {
        return 0;
    }

    /* Raw is in 0.1K. Convert to 0.1°C: subtract 2731 (273.15K × 10) */
    return (int16_t)((int32_t)raw - 2731);
}

/* ── Read current (CC2) ────────────────────────────────────────────── */
int32_t bq76952_read_current(uint8_t module_id)
{
    uint16_t raw;
    if (read_reg16(module_id, BQ76952_REG_CC2_CURRENT, &raw) != 0) {
        return 0;
    }
    /* CC2 current: signed 16-bit, units: mA */
    return (int32_t)(int16_t)raw;
}

/* ── Read safety status registers ──────────────────────────────────── */
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

/* ── Enter/exit config update mode ─────────────────────────────────── */
int32_t bq76952_enter_config(uint8_t module_id)
{
    return bq76952_subcommand(module_id, BQ76952_SUBCMD_SET_CFGUPDATE);
}

int32_t bq76952_exit_config(uint8_t module_id)
{
    return bq76952_subcommand(module_id, BQ76952_SUBCMD_EXIT_CFGUPDATE);
}

/* ── Data memory write with checksum (§12.2) ───────────────────────── */
int32_t bq76952_write_data_memory(uint8_t module_id, uint16_t addr,
                                   const uint8_t *data, uint8_t len)
{
    uint8_t addr_u8 = module_i2c_addr(module_id);
    uint8_t buf[36]; /* max: 1 reg + 2 addr + 32 data */
    uint8_t cksum_buf[34]; /* addr_low + addr_high + data */
    uint8_t total_len;
    int32_t rc;

    if (len == 0U || len > 32U) {
        return -1;
    }

    /* Step 1: Write address + data to 0x3E */
    buf[0] = BQ76952_REG_SUBCMD_LOW;
    buf[1] = (uint8_t)(addr & 0xFFU);
    buf[2] = (uint8_t)((addr >> 8U) & 0xFFU);
    memcpy(&buf[3], data, len);

    total_len = 3U + len;
    rc = hal_i2c_write(addr_u8, buf, total_len);
    if (rc != 0) {
        return rc;
    }

    /* Step 2: Compute checksum over addr bytes + data */
    cksum_buf[0] = buf[1]; /* addr low */
    cksum_buf[1] = buf[2]; /* addr high */
    memcpy(&cksum_buf[2], data, len);

    uint8_t checksum = bq76952_compute_checksum(cksum_buf, 2U + len);

    /* Step 3: Write checksum to 0x60 and length to 0x61 */
    buf[0] = BQ76952_REG_SUBCMD_CKSUM;
    buf[1] = checksum;
    buf[2] = 4U + len; /* length = 4 (addr + checksum + len byte) + data */

    rc = hal_i2c_write(addr_u8, buf, 3U);
    return rc;
}
