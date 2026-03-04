/**
 * @file bms_state.h
 * @brief 7-mode pack state machine
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P2-09: EMS watchdog in READY state (Dave)
 *   P1-05: Post-fire manual-only reset (Henrik, Priya)
 */

#ifndef BMS_STATE_H
#define BMS_STATE_H

#include "bms_types.h"
#include "bms_contactor.h"
#include "bms_protection.h"
#include "bms_safety_io.h"

void bms_state_init(bms_pack_data_t *pack);

void bms_state_run(bms_pack_data_t *pack,
                    bms_contactor_ctx_t *contactor,
                    bms_protection_state_t *prot,
                    const bms_safety_io_state_t *sio,
                    const bms_ems_command_t *cmd,
                    uint32_t dt_ms);

const char *bms_state_mode_name(bms_pack_mode_t mode);

void bms_state_enter_fault(bms_pack_data_t *pack,
                            bms_contactor_ctx_t *contactor);

#endif /* BMS_STATE_H */
