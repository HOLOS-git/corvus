/**
 * bms_bq76952.h — TI BQ76952 cell monitor driver
 *
 * Register addresses and command sequences from:
 *   TI BQ76952 Technical Reference Manual (SLUUBY2B)
 *   §4.1: Direct Commands
 *   §12.2: Data Memory Access
 *
 * Reference driver: skriachko/bq76952 (MIT, STM32WB55)
 *
 * I2C address: 0x08 (7-bit) = 0x10 write / 0x11 read
 * Cell voltage direct commands: 0x14 + (cell_idx * 2)
 * Safety registers: 0x02–0x06
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_BQ76952_H
#define BMS_BQ76952_H

#include <stdint.h>
#include <stdbool.h>
#include "bms_types.h"

/* ── I2C address (7-bit) ───────────────────────────────────────────── */
#define BQ76952_I2C_ADDR           0x08U

/* ── Direct command registers (§4.1) ───────────────────────────────── */
#define BQ76952_REG_SAFETY_ALERT_A 0x02U
#define BQ76952_REG_SAFETY_STATUS_A 0x03U
#define BQ76952_REG_SAFETY_ALERT_B 0x04U
#define BQ76952_REG_SAFETY_STATUS_B 0x05U
#define BQ76952_REG_SAFETY_ALERT_C 0x06U
#define BQ76952_REG_BATTERY_STATUS 0x12U
#define BQ76952_REG_CELL1_VOLTAGE  0x14U  /* Cell 1–16: 0x14–0x32        */
#define BQ76952_REG_STACK_VOLTAGE  0x34U
#define BQ76952_REG_PACK_VOLTAGE   0x36U
#define BQ76952_REG_CC2_CURRENT    0x3AU  /* Coulomb counter 2 current   */
#define BQ76952_REG_INT_TEMP       0x68U  /* internal temp (0.1K)        */
#define BQ76952_REG_TS1_TEMP       0x70U  /* thermistor 1 (0.1K)        */
#define BQ76952_REG_TS2_TEMP       0x72U
#define BQ76952_REG_TS3_TEMP       0x74U

/* ── Subcommand registers (§12.2) ──────────────────────────────────── */
#define BQ76952_REG_SUBCMD_LOW     0x3EU
#define BQ76952_REG_SUBCMD_HIGH    0x3FU
#define BQ76952_REG_SUBCMD_DATA    0x40U  /* 32-byte data buffer         */
#define BQ76952_REG_SUBCMD_CKSUM   0x60U  /* checksum                    */
#define BQ76952_REG_SUBCMD_LEN     0x61U  /* data length                 */

/* ── Subcommands ───────────────────────────────────────────────────── */
#define BQ76952_SUBCMD_DEVICE_NUMBER    0x0001U
#define BQ76952_SUBCMD_FW_VERSION       0x0002U
#define BQ76952_SUBCMD_RESET            0x0012U
#define BQ76952_SUBCMD_SET_CFGUPDATE    0x0090U
#define BQ76952_SUBCMD_EXIT_CFGUPDATE   0x0092U

/* ── Safety Status A bitfields (reg 0x03) ──────────────────────────── */
#define BQ_SSA_SC_DCHG   (1U << 0)  /* Short circuit discharge        */
#define BQ_SSA_OC2_DCHG  (1U << 1)  /* Overcurrent 2 discharge        */
#define BQ_SSA_OC1_DCHG  (1U << 2)  /* Overcurrent 1 discharge        */
#define BQ_SSA_OC_CHG    (1U << 3)  /* Overcurrent charge             */
#define BQ_SSA_CELL_OV   (1U << 4)  /* Cell overvoltage               */
#define BQ_SSA_CELL_UV   (1U << 5)  /* Cell undervoltage              */

/* ── Safety Status B bitfields (reg 0x05) ──────────────────────────── */
#define BQ_SSB_OTF       (1U << 0)  /* Overtemp FET                   */
#define BQ_SSB_OTINT     (1U << 1)  /* Overtemp internal              */
#define BQ_SSB_OTD       (1U << 2)  /* Overtemp discharge             */
#define BQ_SSB_OTC       (1U << 3)  /* Overtemp charge                */
#define BQ_SSB_UTINT     (1U << 4)  /* Undertemp internal             */
#define BQ_SSB_UTD       (1U << 5)  /* Undertemp discharge            */
#define BQ_SSB_UTC       (1U << 6)  /* Undertemp charge               */

/* ── Data memory addresses (configuration) ─────────────────────────── */
#define BQ76952_DM_VCELL_MODE         0x9304U
#define BQ76952_DM_ENABLE_PROT_A      0x9261U
#define BQ76952_DM_ENABLE_PROT_B      0x9262U
#define BQ76952_DM_ENABLE_PROT_C      0x9263U
#define BQ76952_DM_SCD_THRESHOLD      0x9286U
#define BQ76952_DM_SCD_DELAY          0x9287U
#define BQ76952_DM_FET_OPTIONS        0x9308U

/* ── Macro: cell voltage register address ──────────────────────────── */
#define BQ76952_CELL_REG(cell_idx) \
    ((uint8_t)(BQ76952_REG_CELL1_VOLTAGE + ((cell_idx) * 2U)))

/* ── API ───────────────────────────────────────────────────────────── */

/**
 * Initialize BQ76952 for given module.
 * @param module_id  module index 0..(BMS_NUM_MODULES-1)
 * @return 0 on success
 */
int32_t bq76952_init(uint8_t module_id);

/**
 * Read single cell voltage.
 * @param module_id  module index
 * @param cell_idx   cell index within module (0..13 for Orca)
 * @return voltage in mV, or 0 on error
 */
uint16_t bq76952_read_cell_voltage(uint8_t module_id, uint8_t cell_idx);

/**
 * Read all cell voltages for a module.
 * @param module_id  module index
 * @param out_mv     output array of BMS_SE_PER_MODULE voltages
 * @return 0 on success
 */
int32_t bq76952_read_all_cells(uint8_t module_id, uint16_t *out_mv);

/**
 * Read stack voltage for module.
 * @return stack voltage in mV
 */
uint16_t bq76952_read_stack_voltage(uint8_t module_id);

/**
 * Read thermistor temperature.
 * @param module_id   module index
 * @param sensor_idx  0=TS1, 1=TS2, 2=TS3
 * @return temperature in 0.1°C
 */
int16_t bq76952_read_temperature(uint8_t module_id, uint8_t sensor_idx);

/**
 * Read CC2 current measurement.
 * @param module_id  module index
 * @return current in mA (positive = charging)
 */
int32_t bq76952_read_current(uint8_t module_id);

/**
 * Read safety status registers.
 * @param module_id  module index
 * @param out        output safety struct
 * @return 0 on success
 */
int32_t bq76952_read_safety(uint8_t module_id, bms_bq_safety_t *out);

/**
 * Enter configuration update mode (subcmd 0x0090).
 */
int32_t bq76952_enter_config(uint8_t module_id);

/**
 * Exit configuration update mode (subcmd 0x0092).
 */
int32_t bq76952_exit_config(uint8_t module_id);

/**
 * Write to data memory with checksum (§12.2).
 * @param module_id  module index
 * @param addr       16-bit data memory address
 * @param data       data bytes to write
 * @param len        number of bytes (1–32)
 * @return 0 on success
 */
int32_t bq76952_write_data_memory(uint8_t module_id, uint16_t addr,
                                   const uint8_t *data, uint8_t len);

/**
 * Send subcommand (2-byte command via 0x3E/0x3F).
 */
int32_t bq76952_subcommand(uint8_t module_id, uint16_t subcmd);

/**
 * Compute checksum for data memory write per TRM §12.2.
 * checksum = ~(sum of all bytes) & 0xFF
 */
uint8_t bq76952_compute_checksum(const uint8_t *data, uint8_t len);

#endif /* BMS_BQ76952_H */
