/**
 * bms_monitor.h — 10ms periodic monitoring task
 *
 * Reads all cell voltages and temperatures from BQ76952 ASICs,
 * aggregates into pack-level data, detects cell imbalance.
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_MONITOR_H
#define BMS_MONITOR_H

#include "bms_types.h"

/** Scan complete flag — set after all 22 modules have been read once */
#define BMS_MONITOR_SCAN_COMPLETE  0x01U

/**
 * Initialize monitoring subsystem.
 * @param pack  pointer to global pack data structure
 */
void bms_monitor_init(bms_pack_data_t *pack);

/**
 * Execute one monitoring cycle (called every 10ms).
 * Reads ONE module per cycle (staggered I2C). After all 22 modules
 * have been read (one full scan = 220ms), recalculates pack aggregates.
 */
void bms_monitor_run(bms_pack_data_t *pack);

/**
 * Read a single module and update its pack data.
 * @param pack       pack data
 * @param module_id  module to read (0..BMS_NUM_MODULES-1)
 */
void bms_monitor_read_module(bms_pack_data_t *pack, uint8_t module_id);

/**
 * Read all modules and update pack data (batch — for testing).
 * Separated from run() for testability.
 */
void bms_monitor_read_modules(bms_pack_data_t *pack);

/**
 * Aggregate cell voltages: compute min, max, avg, pack total.
 */
void bms_monitor_aggregate(bms_pack_data_t *pack);

/**
 * Get current scan module index (0..BMS_NUM_MODULES-1).
 */
uint8_t bms_monitor_get_scan_index(void);

/**
 * Check if a full scan has just completed.
 */
bool bms_monitor_scan_complete(void);

/**
 * Get total completed scan count.
 */
uint32_t bms_monitor_get_scan_count(void);

#endif /* BMS_MONITOR_H */
