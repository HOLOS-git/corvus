/**
 * bms_state.c — 7-mode pack state machine
 *
 * Transitions per Orca ESS Integrator Manual §7.1:
 *
 *   Power-on → NOT_READY (self-test)
 *   NOT_READY → READY (self-test pass) | FAULT (self-test fail)
 *   READY → CONNECTING (EMS connect cmd) | POWER_SAVE (EMS cmd) | FAULT
 *   CONNECTING → CONNECTED (pre-charge complete) | READY (timeout) | FAULT
 *   CONNECTED → READY (EMS disconnect) | FAULT
 *   POWER_SAVE → READY (EMS wake cmd)
 *   FAULT → READY (manual reset after safe-state hold)
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include "bms_state.h"
#include "bms_contactor.h"
#include "bms_protection.h"
#include "bms_config.h"
#include <string.h>

/* ── Mode name lookup ──────────────────────────────────────────────── */

static const char *mode_names[] = {
    "OFF", "POWER_SAVE", "FAULT", "READY",
    "CONNECTING", "CONNECTED", "NOT_READY"
};

const char *bms_state_mode_name(bms_pack_mode_t mode)
{
    if ((uint8_t)mode <= 6U) {
        return mode_names[(uint8_t)mode];
    }
    return "UNKNOWN";
}

/* ── Init ──────────────────────────────────────────────────────────── */

void bms_state_init(bms_pack_data_t *pack)
{
    pack->mode = BMS_MODE_NOT_READY;
}

/* ── Force fault ───────────────────────────────────────────────────── */

void bms_state_enter_fault(bms_pack_data_t *pack,
                            bms_contactor_ctx_t *contactor)
{
    pack->mode = BMS_MODE_FAULT;
    pack->charge_limit_ma = 0;
    pack->discharge_limit_ma = 0;
    bms_contactor_request_open(contactor);
}

/* ── State machine run ─────────────────────────────────────────────── */

void bms_state_run(bms_pack_data_t *pack,
                    bms_contactor_ctx_t *contactor,
                    bms_protection_state_t *prot,
                    const bms_ems_command_t *cmd,
                    uint32_t dt_ms)
{
    (void)dt_ms;

    /* ── Global: any fault latched → enter FAULT from any state ────── */
    if (pack->fault_latched && pack->mode != BMS_MODE_FAULT) {
        BMS_LOG("State: %s -> FAULT (fault latched)", bms_state_mode_name(pack->mode));
        bms_state_enter_fault(pack, contactor);
        return;
    }

    /* ── EMS watchdog: if no message for EMS_WATCHDOG_MS → safe state ─ */
    if (pack->mode == BMS_MODE_CONNECTED || pack->mode == BMS_MODE_CONNECTING) {
        uint32_t now = pack->uptime_ms;
        uint32_t elapsed = now - pack->last_ems_msg_ms;
        if (elapsed > BMS_EMS_WATCHDOG_MS) {
            BMS_LOG("State: EMS watchdog expired (%u ms)", elapsed);
            pack->faults.ems_timeout = 1U;
            bms_state_enter_fault(pack, contactor);
            return;
        }
    }

    switch (pack->mode) {

    case BMS_MODE_NOT_READY:
        /* Self-test: verify all modules responding */
        {
            bool all_ok = true;
            uint8_t mod;
            for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
                if (!pack->modules[mod].comm_ok) {
                    all_ok = false;
                    break;
                }
            }
            if (all_ok) {
                pack->mode = BMS_MODE_READY;
                BMS_LOG("State: NOT_READY -> READY");
            }
        }
        break;

    case BMS_MODE_READY:
        if (cmd != NULL) {
            switch (cmd->type) {
            case EMS_CMD_CONNECT_CHG:
            case EMS_CMD_CONNECT_DCHG:
                /* Request contactor close */
                bms_contactor_request_close(contactor, pack->pack_voltage_mv);
                pack->mode = BMS_MODE_CONNECTING;
                pack->last_ems_msg_ms = pack->uptime_ms;
                BMS_LOG("State: READY -> CONNECTING");
                break;

            case EMS_CMD_POWER_SAVE:
                pack->mode = BMS_MODE_POWER_SAVE;
                BMS_LOG("State: READY -> POWER_SAVE");
                break;

            default:
                break;
            }
        }
        break;

    case BMS_MODE_CONNECTING:
        /* Update EMS watchdog on any command */
        if (cmd != NULL && cmd->type != EMS_CMD_NONE) {
            pack->last_ems_msg_ms = cmd->timestamp_ms;
        }

        /* Check contactor state */
        if (bms_contactor_get_state(contactor) == CONTACTOR_CLOSED) {
            pack->mode = BMS_MODE_CONNECTED;
            BMS_LOG("State: CONNECTING -> CONNECTED");
        }
        else if (bms_contactor_get_state(contactor) == CONTACTOR_OPEN) {
            /* Pre-charge failed or aborted */
            pack->mode = BMS_MODE_READY;
            BMS_LOG("State: CONNECTING -> READY (contactor open)");
        }
        else if (bms_contactor_is_faulted(contactor)) {
            bms_state_enter_fault(pack, contactor);
        }

        /* EMS disconnect while connecting */
        if (cmd != NULL && cmd->type == EMS_CMD_DISCONNECT) {
            bms_contactor_request_open(contactor);
            pack->mode = BMS_MODE_READY;
        }
        break;

    case BMS_MODE_CONNECTED:
        if (cmd != NULL) {
            pack->last_ems_msg_ms = cmd->timestamp_ms;

            if (cmd->type == EMS_CMD_DISCONNECT) {
                bms_contactor_request_open(contactor);
                pack->mode = BMS_MODE_READY;
                BMS_LOG("State: CONNECTED -> READY (disconnect)");
            }
            else if (cmd->type == EMS_CMD_SET_LIMITS) {
                /* Clamp EMS limits to protection limits */
                if (cmd->charge_limit_ma < pack->charge_limit_ma) {
                    pack->charge_limit_ma = cmd->charge_limit_ma;
                }
                if (cmd->discharge_limit_ma < pack->discharge_limit_ma) {
                    pack->discharge_limit_ma = cmd->discharge_limit_ma;
                }
            }
            else if (cmd->type == EMS_CMD_RESET_FAULTS) {
                /* No-op in CONNECTED (not faulted) */
            }
        }

        /* Check contactor health */
        if (bms_contactor_is_faulted(contactor)) {
            bms_state_enter_fault(pack, contactor);
        }
        break;

    case BMS_MODE_POWER_SAVE:
        /* Wake on any EMS command except POWER_SAVE */
        if (cmd != NULL && cmd->type != EMS_CMD_NONE &&
            cmd->type != EMS_CMD_POWER_SAVE) {
            pack->mode = BMS_MODE_READY;
            BMS_LOG("State: POWER_SAVE -> READY (wake)");
        }
        break;

    case BMS_MODE_FAULT:
        /* Only transition out via manual fault reset */
        if (cmd != NULL && cmd->type == EMS_CMD_RESET_FAULTS) {
            if (bms_protection_can_reset(prot, pack)) {
                bms_protection_reset(prot, pack);
                pack->mode = BMS_MODE_READY;
                BMS_LOG("State: FAULT -> READY (reset)");
            } else {
                BMS_LOG("State: FAULT reset denied (safe-state hold incomplete)");
            }
        }
        break;

    case BMS_MODE_OFF:
        /* Power-on → NOT_READY handled by init */
        break;
    }
}
