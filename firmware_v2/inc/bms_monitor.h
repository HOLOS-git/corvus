/**
 * @file bms_monitor.h
 * @brief Cell monitoring with sensor fault detection
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P0-01: Sensor fault detection — sentinel, cross-check, consecutive (Dave, Priya)
 *   P0-02: I2C failure counting and fault latching (Dave, Catherine)
 *   P2-07: Stack vs cells cross-check, plausibility (Dave, Yara, Priya)
 */

#ifndef BMS_MONITOR_H
#define BMS_MONITOR_H

#include "bms_types.h"

void     bms_monitor_init(bms_pack_data_t *pack);
void     bms_monitor_run(bms_pack_data_t *pack);
void     bms_monitor_read_module(bms_pack_data_t *pack, uint8_t module_id);
void     bms_monitor_aggregate(bms_pack_data_t *pack);
uint8_t  bms_monitor_get_scan_index(void);
bool     bms_monitor_scan_complete(void);
uint32_t bms_monitor_get_scan_count(void);

#endif /* BMS_MONITOR_H */
