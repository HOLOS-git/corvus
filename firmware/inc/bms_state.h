/**
 * bms_state.h — 7-mode pack state machine
 *
 * Modes per Orca ESS Integrator Manual §7.1, Table 15:
 *   OFF → NOT_READY → READY → CONNECTING → CONNECTED
 *                         ↕         ↓
 *                    POWER_SAVE   FAULT
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_STATE_H
#define BMS_STATE_H

#include "bms_types.h"
#include "bms_contactor.h"
#include "bms_protection.h"

/**
 * Initialize state machine — starts in NOT_READY.
 */
void bms_state_init(bms_pack_data_t *pack);

/**
 * Run state machine — called every 100ms.
 * Evaluates current pack data, EMS commands, contactor state,
 * and transitions pack mode accordingly.
 *
 * @param pack      pack data (reads/writes mode, faults)
 * @param contactor contactor context
 * @param prot      protection state
 * @param cmd       latest EMS command (or NULL)
 * @param dt_ms     time since last call
 */
void bms_state_run(bms_pack_data_t *pack,
                    bms_contactor_ctx_t *contactor,
                    bms_protection_state_t *prot,
                    const bms_ems_command_t *cmd,
                    uint32_t dt_ms);

/**
 * Get human-readable name for pack mode.
 */
const char *bms_state_mode_name(bms_pack_mode_t mode);

/**
 * Force transition to FAULT mode (called by protection or contactor).
 */
void bms_state_enter_fault(bms_pack_data_t *pack,
                            bms_contactor_ctx_t *contactor);

#endif /* BMS_STATE_H */
