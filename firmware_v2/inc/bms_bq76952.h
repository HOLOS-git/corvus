/**
 * @file bms_bq76952.h
 * @brief TI BQ76952 AFE driver — with HW protection configuration
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P1-01: Autonomous HW protection enable (Henrik SHOWSTOPPER, Mikael, Catherine, Priya)
 *   P0-01: Temperature reads return sentinel on failure (Dave, Priya)
 *   P2-07: Read-back verify configuration writes (Dave, Yara, Priya)
 *
 * Reference: TI BQ76952 TRM (SLUUBY2B)
 */

#ifndef BMS_BQ76952_H
#define BMS_BQ76952_H

#include <stdint.h>
#include <stdbool.h>
#include "bms_types.h"

/* ── I2C address ───────────────────────────────────────────────────── */
#define BQ76952_I2C_ADDR           0x08U

/* ── Direct command registers ──────────────────────────────────────── */
#define BQ76952_REG_SAFETY_ALERT_A  0x02U
#define BQ76952_REG_SAFETY_STATUS_A 0x03U
#define BQ76952_REG_SAFETY_ALERT_B  0x04U
#define BQ76952_REG_SAFETY_STATUS_B 0x05U
#define BQ76952_REG_SAFETY_ALERT_C  0x06U
#define BQ76952_REG_BATTERY_STATUS  0x12U
#define BQ76952_REG_CELL1_VOLTAGE   0x14U
#define BQ76952_REG_STACK_VOLTAGE   0x34U
#define BQ76952_REG_PACK_VOLTAGE    0x36U
#define BQ76952_REG_CC2_CURRENT     0x3AU
#define BQ76952_REG_INT_TEMP        0x68U
#define BQ76952_REG_TS1_TEMP        0x70U
#define BQ76952_REG_TS2_TEMP        0x72U
#define BQ76952_REG_TS3_TEMP        0x74U

/* ── Subcommand registers ──────────────────────────────────────────── */
#define BQ76952_REG_SUBCMD_LOW      0x3EU
#define BQ76952_REG_SUBCMD_HIGH     0x3FU
#define BQ76952_REG_SUBCMD_DATA     0x40U
#define BQ76952_REG_SUBCMD_CKSUM    0x60U
#define BQ76952_REG_SUBCMD_LEN      0x61U

/* ── Subcommands ───────────────────────────────────────────────────── */
#define BQ76952_SUBCMD_DEVICE_NUMBER    0x0001U
#define BQ76952_SUBCMD_FW_VERSION       0x0002U
#define BQ76952_SUBCMD_RESET            0x0012U
#define BQ76952_SUBCMD_SET_CFGUPDATE    0x0090U
#define BQ76952_SUBCMD_EXIT_CFGUPDATE   0x0092U

/* ── Safety Status A bitfields ─────────────────────────────────────── */
#define BQ_SSA_SC_DCHG    (1U << 0)
#define BQ_SSA_OC2_DCHG   (1U << 1)
#define BQ_SSA_OC1_DCHG   (1U << 2)
#define BQ_SSA_OC_CHG     (1U << 3)
#define BQ_SSA_CELL_OV    (1U << 4)
#define BQ_SSA_CELL_UV    (1U << 5)

/* ── Safety Status B bitfields ─────────────────────────────────────── */
#define BQ_SSB_OTF        (1U << 0)
#define BQ_SSB_OTINT      (1U << 1)
#define BQ_SSB_OTD        (1U << 2)
#define BQ_SSB_OTC        (1U << 3)
#define BQ_SSB_UTINT      (1U << 4)
#define BQ_SSB_UTD        (1U << 5)
#define BQ_SSB_UTC        (1U << 6)

/* ── Data memory addresses ─────────────────────────────────────────── */
#define BQ76952_DM_VCELL_MODE          0x9304U
#define BQ76952_DM_ENABLE_PROT_A       0x9261U
#define BQ76952_DM_ENABLE_PROT_B       0x9262U
#define BQ76952_DM_ENABLE_PROT_C       0x9263U
#define BQ76952_DM_COV_THRESHOLD       0x9278U  /* Cell OV threshold */
#define BQ76952_DM_CUV_THRESHOLD       0x9280U  /* Cell UV threshold */
#define BQ76952_DM_OTC_THRESHOLD       0x9290U  /* OT charge threshold */
#define BQ76952_DM_OTD_THRESHOLD       0x9292U  /* OT discharge threshold */
#define BQ76952_DM_SCD_THRESHOLD       0x9286U
#define BQ76952_DM_SCD_DELAY           0x9287U
#define BQ76952_DM_FET_OPTIONS         0x9308U

/* ── P1-01: Protection Enable Bitmasks ─────────────────────────────── */
#define BQ_PROT_A_SC_DCHG    (1U << 0)
#define BQ_PROT_A_OC2_DCHG   (1U << 1)
#define BQ_PROT_A_OC1_DCHG   (1U << 2)
#define BQ_PROT_A_OC_CHG     (1U << 3)
#define BQ_PROT_A_CELL_OV    (1U << 4)
#define BQ_PROT_A_CELL_UV    (1U << 5)

#define BQ_PROT_B_OTF        (1U << 0)
#define BQ_PROT_B_OTINT      (1U << 1)
#define BQ_PROT_B_OTD        (1U << 2)
#define BQ_PROT_B_OTC        (1U << 3)
#define BQ_PROT_B_UTD        (1U << 5)
#define BQ_PROT_B_UTC        (1U << 6)

/* ── Cell register macro ───────────────────────────────────────────── */
#define BQ76952_CELL_REG(cell_idx) \
    ((uint8_t)(BQ76952_REG_CELL1_VOLTAGE + ((cell_idx) * 2U)))

/* ── API ───────────────────────────────────────────────────────────── */

int32_t  bq76952_init(uint8_t module_id);
uint16_t bq76952_read_cell_voltage(uint8_t module_id, uint8_t cell_idx);
int32_t  bq76952_read_all_cells(uint8_t module_id, uint16_t *out_mv);
uint16_t bq76952_read_stack_voltage(uint8_t module_id);

/**
 * Read thermistor temperature.
 * P0-01: Returns BMS_TEMP_SENSOR_SENTINEL on I2C failure (not 0).
 */
int16_t  bq76952_read_temperature(uint8_t module_id, uint8_t sensor_idx);

int32_t  bq76952_read_current(uint8_t module_id);
int32_t  bq76952_read_safety(uint8_t module_id, bms_bq_safety_t *out);
int32_t  bq76952_enter_config(uint8_t module_id);
int32_t  bq76952_exit_config(uint8_t module_id);
int32_t  bq76952_write_data_memory(uint8_t module_id, uint16_t addr,
                                    const uint8_t *data, uint8_t len);
int32_t  bq76952_read_data_memory(uint8_t module_id, uint16_t addr,
                                   uint8_t *data, uint8_t len);
int32_t  bq76952_subcommand(uint8_t module_id, uint16_t subcmd);
uint8_t  bq76952_compute_checksum(const uint8_t *data, uint8_t len);

/**
 * P1-01: Configure autonomous hardware protection on BQ76952.
 * Writes ENABLE_PROT_A/B/C and OV/UV/OT thresholds.
 * Performs read-back verify (P2-07).
 * @return 0 on success, negative on failure
 *
 * Safety rationale: BQ76952 can independently trip FETs even if MCU hangs.
 * This is the critical second layer that makes HW safety truly independent.
 * Ref: Henrik SHOWSTOPPER, Mikael, Catherine, Priya
 */
int32_t  bq76952_configure_hw_protection(uint8_t module_id);

#endif /* BMS_BQ76952_H */
