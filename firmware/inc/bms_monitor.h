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

/**
 * Initialize monitoring subsystem.
 * @param pack  pointer to global pack data structure
 */
void bms_monitor_init(bms_pack_data_t *pack);

/**
 * Execute one monitoring cycle (called every 10ms).
 * Reads all BQ76952 ASICs, updates cell_mv[], temps,
 * aggregated voltages, and cell imbalance status.
 */
void bms_monitor_run(bms_pack_data_t *pack);

/**
 * Read all modules and update pack data.
 * Separated from run() for testability.
 */
void bms_monitor_read_modules(bms_pack_data_t *pack);

/**
 * Aggregate cell voltages: compute min, max, avg, pack total.
 */
void bms_monitor_aggregate(bms_pack_data_t *pack);

#endif /* BMS_MONITOR_H */
