/**
 * @file bms_state.c
 * @brief 7-mode pack state machine
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P0-03: State machine passes bus_voltage_mv to contactor (Dave)
 *   P2-09: EMS watchdog in READY state — 30min → POWER_SAVE (Dave)
 *   P1-05: Post-fire manual-only reset (Henrik, Priya)
 *   P1-03..06: Safety I/O integration — inhibit close on vent failure
 */

#include "bms_state.h"
#include "bms_config.h"

static const char *mode_names[] = {
    "OFF", "POWER_SAVE", "FAULT", "READY",
    "CONNECTING", "CONNECTED", "NOT_READY"
};

const char *bms_state_mode_name(bms_pack_mode_t mode)
{
    if ((uint8_t)mode < BMS_MODE_COUNT) { return mode_names[(uint8_t)mode]; }
    return "UNKNOWN";
}

void bms_state_init(bms_pack_data_t *pack)
{
    pack->mode = BMS_MODE_NOT_READY;
}

void bms_state_enter_fault(bms_pack_data_t *pack,
                            bms_contactor_ctx_t *contactor)
{
    pack->mode = BMS_MODE_FAULT;
    pack->charge_limit_ma = 0;
    pack->discharge_limit_ma = 0;
    bms_contactor_request_open(contactor);
}

void bms_state_run(bms_pack_data_t *pack,
                    bms_contactor_ctx_t *contactor,
                    bms_protection_state_t *prot,
                    const bms_safety_io_state_t *sio,
                    const bms_ems_command_t *cmd,
                    uint32_t dt_ms)
{
    (void)dt_ms;

    /* Global: fault latched → FAULT from any state */
    if (pack->fault_latched && pack->mode != BMS_MODE_FAULT) {
        BMS_LOG("State: %s -> FAULT", bms_state_mode_name(pack->mode));
        bms_state_enter_fault(pack, contactor);
        return;
    }

    /* P1-03..06: Safety I/O emergency → immediate FAULT */
    if (sio != NULL && bms_safety_io_emergency_shutdown(sio) &&
        pack->mode != BMS_MODE_FAULT) {
        BMS_LOG("State: Safety I/O emergency shutdown");
        bms_state_enter_fault(pack, contactor);
        return;
    }

    /* EMS watchdog in CONNECTED/CONNECTING */
    if (pack->mode == BMS_MODE_CONNECTED || pack->mode == BMS_MODE_CONNECTING) {
        uint32_t elapsed = pack->uptime_ms - pack->last_ems_msg_ms;
        if (elapsed > BMS_EMS_WATCHDOG_MS) {
            pack->faults.ems_timeout = 1U;
            bms_state_enter_fault(pack, contactor);
            return;
        }
    }

    /* P2-09: EMS watchdog in READY — 30 min → POWER_SAVE (Dave) */
    if (pack->mode == BMS_MODE_READY) {
        uint32_t elapsed = pack->uptime_ms - pack->last_ems_msg_ms;
        if (elapsed > BMS_EMS_READY_TIMEOUT_MS) {
            pack->mode = BMS_MODE_POWER_SAVE;
            BMS_LOG("P2-09: READY timeout (%u ms) -> POWER_SAVE", elapsed);
        }
    }

    switch (pack->mode) {

    case BMS_MODE_NOT_READY:
        {
            bool all_ok = true;
            uint8_t mod;
            for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
                if (!pack->modules[mod].comm_ok) { all_ok = false; break; }
            }
            if (all_ok) {
                pack->mode = BMS_MODE_READY;
                pack->last_ems_msg_ms = pack->uptime_ms; /* P2-09: start timer */
            }
        }
        break;

    case BMS_MODE_READY:
        if (cmd != NULL && cmd->valid) {
            switch (cmd->type) {
            case EMS_CMD_CONNECT_CHG:
            case EMS_CMD_CONNECT_DCHG:
                /* P1-04: Check safety I/O before closing */
                if (sio != NULL && bms_safety_io_inhibit_close(sio)) {
                    BMS_LOG("State: Close inhibited by safety I/O");
                    break;
                }
                /* P0-03: Pass BUS voltage (not pack voltage) to contactor */
                bms_contactor_request_close(contactor, pack->bus_voltage_mv);
                pack->mode = BMS_MODE_CONNECTING;
                pack->last_ems_msg_ms = pack->uptime_ms;
                break;
            case EMS_CMD_POWER_SAVE:
                pack->mode = BMS_MODE_POWER_SAVE;
                break;
            default:
                break;
            }
        }
        break;

    case BMS_MODE_CONNECTING:
        if (cmd != NULL && cmd->valid && cmd->type != EMS_CMD_NONE) {
            pack->last_ems_msg_ms = cmd->timestamp_ms;
        }
        if (bms_contactor_get_state(contactor) == CONTACTOR_CLOSED) {
            pack->mode = BMS_MODE_CONNECTED;
        } else if (bms_contactor_get_state(contactor) == CONTACTOR_OPEN) {
            pack->mode = BMS_MODE_READY;
        } else if (bms_contactor_is_faulted(contactor)) {
            bms_state_enter_fault(pack, contactor);
        }
        if (cmd != NULL && cmd->valid && cmd->type == EMS_CMD_DISCONNECT) {
            bms_contactor_request_open(contactor);
            pack->mode = BMS_MODE_READY;
        }
        break;

    case BMS_MODE_CONNECTED:
        if (cmd != NULL && cmd->valid) {
            pack->last_ems_msg_ms = cmd->timestamp_ms;
            if (cmd->type == EMS_CMD_DISCONNECT) {
                bms_contactor_request_open(contactor);
                pack->mode = BMS_MODE_READY;
            } else if (cmd->type == EMS_CMD_SET_LIMITS) {
                /* P2-06: Limits already validated by CAN decoder */
                if (cmd->charge_limit_ma < pack->charge_limit_ma) {
                    pack->charge_limit_ma = cmd->charge_limit_ma;
                }
                if (cmd->discharge_limit_ma < pack->discharge_limit_ma) {
                    pack->discharge_limit_ma = cmd->discharge_limit_ma;
                }
            }
        }
        if (bms_contactor_is_faulted(contactor)) {
            bms_state_enter_fault(pack, contactor);
        }
        break;

    case BMS_MODE_POWER_SAVE:
        if (cmd != NULL && cmd->valid &&
            cmd->type != EMS_CMD_NONE && cmd->type != EMS_CMD_POWER_SAVE) {
            pack->mode = BMS_MODE_READY;
            pack->last_ems_msg_ms = pack->uptime_ms;
        }
        break;

    case BMS_MODE_FAULT:
        if (cmd != NULL && cmd->valid && cmd->type == EMS_CMD_RESET_FAULTS) {
            if (bms_protection_can_reset(prot, pack)) {
                bms_protection_reset(prot, pack);
                pack->mode = BMS_MODE_READY;
                pack->last_ems_msg_ms = pack->uptime_ms;
            }
        }
        break;

    case BMS_MODE_OFF:
        break;
    }
}
