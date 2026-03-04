/**
 * @file bms_can.h
 * @brief CAN interface with input validation
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P2-06: Input validation — range checks, negative rejection (Yara)
 *   CC-01: Noted unauthenticated (all 6 reviewers) — auth deferred to P2-01
 */

#ifndef BMS_CAN_H
#define BMS_CAN_H

#include "bms_types.h"

void bms_can_init(void);

void bms_can_encode_status(const bms_pack_data_t *pack, bms_can_frame_t *frame);
void bms_can_encode_voltages(const bms_pack_data_t *pack, bms_can_frame_t *frame);
void bms_can_encode_temps(const bms_pack_data_t *pack, bms_can_frame_t *frame);
void bms_can_encode_heartbeat(uint32_t uptime_ms, bms_can_frame_t *frame);
void bms_can_encode_limits(const bms_pack_data_t *pack, bms_can_frame_t *frame);
void bms_can_encode_cell_broadcast(const bms_pack_data_t *pack,
                                    uint8_t frame_idx, bms_can_frame_t *frame);

/**
 * P2-06: Decode with full input validation.
 * - Command type validated against enum range
 * - charge/discharge limits clamped to [0, BMS_MAX_*_MA]
 * - Negative values rejected
 * - EMS_CMD_NONE rejected (must be explicit heartbeat on 0x210)
 * - Reserved bytes validated as zero
 */
int32_t bms_can_decode_ems_command(const bms_can_frame_t *frame,
                                    bms_ems_command_t *cmd);

void bms_can_tx_periodic(const bms_pack_data_t *pack);
bool bms_can_rx_process(bms_ems_command_t *cmd);

/**
 * CC-01 / P2-01: CAN authentication stubs.
 * Sequence counter tracking is implemented; AES-128-CMAC deferred.
 * Gated by BMS_CAN_AUTH_ENABLED config flag.
 */
bool bms_can_auth_verify(const bms_can_frame_t *frame);
void bms_can_auth_sign(bms_can_frame_t *frame);
uint16_t bms_can_auth_get_tx_seq(void);
uint16_t bms_can_auth_get_rx_seq(void);

#endif /* BMS_CAN_H */
