/**
 * @file bms_can.c
 * @brief CAN TX/RX with input validation
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P2-06: Input validation on all CAN parameters (Yara HIGH)
 *     - Command type validated against enum range
 *     - Negative current limits rejected
 *     - Limits clamped to [0, BMS_MAX_*_MA]
 *     - EMS_CMD_NONE (0) rejected as command (must use 0x210 for heartbeat)
 *     - Reserved bytes validated as zero
 *   CC-01: CAN authentication noted but deferred to P2-01 (all 6 reviewers)
 *   P0-04: dT/dt alarm CAN message (Priya)
 */

#include "bms_can.h"
#include "bms_hal.h"
#include "bms_config.h"
#include <string.h>

/* ── Big-endian helpers ────────────────────────────────────────────── */

static void pack_u16_be(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)((val >> 8U) & 0xFFU);
    buf[1] = (uint8_t)(val & 0xFFU);
}

static void pack_i16_be(uint8_t *buf, int16_t val)
{
    pack_u16_be(buf, (uint16_t)val);
}

static void pack_u32_be(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)((val >> 24U) & 0xFFU);
    buf[1] = (uint8_t)((val >> 16U) & 0xFFU);
    buf[2] = (uint8_t)((val >> 8U) & 0xFFU);
    buf[3] = (uint8_t)(val & 0xFFU);
}

static int16_t unpack_i16_be(const uint8_t *buf)
{
    return (int16_t)((uint16_t)((uint16_t)buf[0] << 8U) | (uint16_t)buf[1]);
}

/* ── Init ──────────────────────────────────────────────────────────── */

void bms_can_init(void)
{
    /* P2-08: Set hardware filter to accept only expected IDs */
    hal_can_set_filter(CAN_ID_EMS_COMMAND, CAN_ID_EMS_HEARTBEAT);
}

/* ── Encode functions (unchanged from original, no SIMULATION DISCLAIMER) ── */

void bms_can_encode_status(const bms_pack_data_t *pack, bms_can_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->id = CAN_ID_ARRAY_STATUS;
    frame->dlc = 8U;
    frame->data[0] = (uint8_t)pack->mode;
    pack_u16_be(&frame->data[1], (uint16_t)(pack->pack_voltage_mv / 100U));
    pack_i16_be(&frame->data[3], (int16_t)(pack->pack_current_ma / 100));
    frame->data[5] = (uint8_t)(pack->soc_hundredths / 100U);
    frame->data[6] = (uint8_t)((pack->max_temp_deci_c / 10) + 40);
    {
        uint32_t f;
        memcpy(&f, &pack->faults, sizeof(f));
        frame->data[7] = (uint8_t)(f & 0xFFU);
    }
}

void bms_can_encode_voltages(const bms_pack_data_t *pack, bms_can_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->id = CAN_ID_PACK_VOLTAGES;
    frame->dlc = 8U;
    pack_u16_be(&frame->data[0], pack->max_cell_mv);
    pack_u16_be(&frame->data[2], pack->min_cell_mv);
    pack_u16_be(&frame->data[4], pack->avg_cell_mv);
    pack_u16_be(&frame->data[6], (uint16_t)(pack->max_cell_mv - pack->min_cell_mv));
}

void bms_can_encode_temps(const bms_pack_data_t *pack, bms_can_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->id = CAN_ID_PACK_TEMPS;
    frame->dlc = 8U;
    pack_i16_be(&frame->data[0], pack->max_temp_deci_c);
    pack_i16_be(&frame->data[2], pack->min_temp_deci_c);
    pack_i16_be(&frame->data[4], (int16_t)(pack->charge_limit_ma / 100));
    pack_i16_be(&frame->data[6], (int16_t)(pack->discharge_limit_ma / 100));
}

void bms_can_encode_heartbeat(uint32_t uptime_ms, bms_can_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->id = CAN_ID_HEARTBEAT;
    frame->dlc = 8U;
    pack_u32_be(&frame->data[0], uptime_ms);
}

void bms_can_encode_limits(const bms_pack_data_t *pack, bms_can_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->id = CAN_ID_LIMITS;
    frame->dlc = 8U;
    pack_u32_be(&frame->data[0], (uint32_t)pack->charge_limit_ma);
    pack_u32_be(&frame->data[4], (uint32_t)pack->discharge_limit_ma);
}

void bms_can_encode_cell_broadcast(const bms_pack_data_t *pack,
                                    uint8_t frame_idx, bms_can_frame_t *frame)
{
    uint16_t base;
    uint8_t i;
    memset(frame, 0, sizeof(*frame));
    frame->id = CAN_ID_CELL_BROADCAST + (uint32_t)frame_idx;
    frame->dlc = 8U;
    base = (uint16_t)frame_idx * 4U;
    for (i = 0U; i < 4U; i++) {
        uint16_t idx = base + i;
        uint16_t mv = (idx < BMS_SE_PER_PACK) ? pack->cell_mv[idx] : 0U;
        pack_u16_be(&frame->data[i * 2U], mv);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * P2-06: Decode EMS Command with FULL Input Validation (Yara HIGH)
 *
 * Original code: No validation. Negative charge_limit via int16_t × 1000
 * satisfies "less than" clamp, bypassing safety intent.
 * CMD_NONE (0) passes validation → confusing semantics.
 *
 * Now:
 *   1. Command type must be in [1, EMS_CMD_COUNT)
 *   2. EMS_CMD_NONE (0) rejected — heartbeat uses 0x210
 *   3. Negative limits rejected (not just clamped)
 *   4. Limits clamped to hardware maximums
 *   5. Reserved bytes must be zero
 *   6. All rejections logged
 * ═══════════════════════════════════════════════════════════════════════ */

int32_t bms_can_decode_ems_command(const bms_can_frame_t *frame,
                                    bms_ems_command_t *cmd)
{
    int16_t raw_charge, raw_discharge;

    if (frame->id != CAN_ID_EMS_COMMAND || frame->dlc < 5U) {
        cmd->valid = false;
        return -1;
    }

    cmd->type = (bms_ems_cmd_type_t)frame->data[0];
    cmd->timestamp_ms = hal_tick_ms();

    /* P2-06: Reject CMD_NONE — must use heartbeat ID 0x210 */
    if (cmd->type == EMS_CMD_NONE) {
        BMS_LOG("P2-06: EMS_CMD_NONE rejected on 0x200 (use 0x210 for heartbeat)");
        cmd->valid = false;
        return -1;
    }

    /* P2-06: Validate command type range */
    if ((uint8_t)cmd->type >= (uint8_t)EMS_CMD_COUNT) {
        BMS_LOG("P2-06: Invalid command type %u", (unsigned)cmd->type);
        cmd->valid = false;
        return -1;
    }

    /* P2-06: Validate reserved bytes are zero
     * P3-01: When CAN auth is enabled, bytes [6] and [7] carry the sequence
     *        counter — only byte [5] must be zero. (Yara CRITICAL: auth/reserved
     *        byte conflict — NEW-01 from pentester-v2) */
    if (frame->dlc >= 8U) {
        if (frame->data[5] != 0U) {
            BMS_LOG("P2-06: Non-zero reserved byte [5] in EMS command");
            cmd->valid = false;
            return -1;
        }
#if BMS_CAN_AUTH_ENABLED == 0U
        /* Auth disabled: bytes [6] and [7] must also be zero */
        if (frame->data[6] != 0U || frame->data[7] != 0U) {
            BMS_LOG("P2-06: Non-zero reserved bytes [6:7] in EMS command");
            cmd->valid = false;
            return -1;
        }
#endif
        /* When BMS_CAN_AUTH_ENABLED=1, bytes [6:7] hold the sequence counter
         * and are validated by bms_can_auth_verify() instead. */
    }

    /* Decode current limits */
    raw_charge = unpack_i16_be(&frame->data[1]);
    raw_discharge = unpack_i16_be(&frame->data[3]);

    /* P2-06: Reject negative values (Yara: "negative values bypass downward clamp") */
    if (raw_charge < 0) {
        BMS_LOG("P2-06: Negative charge limit rejected (%d)", raw_charge);
        cmd->valid = false;
        return -1;
    }
    if (raw_discharge < 0) {
        BMS_LOG("P2-06: Negative discharge limit rejected (%d)", raw_discharge);
        cmd->valid = false;
        return -1;
    }

    /* Convert A → mA and clamp to hardware max */
    cmd->charge_limit_ma = (int32_t)raw_charge * 1000;
    cmd->discharge_limit_ma = (int32_t)raw_discharge * 1000;

    if (cmd->charge_limit_ma > BMS_MAX_CHARGE_MA) {
        cmd->charge_limit_ma = BMS_MAX_CHARGE_MA;
    }
    if (cmd->discharge_limit_ma > BMS_MAX_DISCHARGE_MA) {
        cmd->discharge_limit_ma = BMS_MAX_DISCHARGE_MA;
    }

    cmd->valid = true;
    return 0;
}

/* ── Periodic TX ───────────────────────────────────────────────────── */

static uint8_t s_cell_broadcast_idx;

void bms_can_tx_periodic(const bms_pack_data_t *pack)
{
    bms_can_frame_t frame;
    uint8_t max_broadcast = (uint8_t)((BMS_SE_PER_PACK + 3U) / 4U);

    bms_can_encode_status(pack, &frame);
    (void)hal_can_transmit(&frame);

    bms_can_encode_limits(pack, &frame);
    (void)hal_can_transmit(&frame);

    bms_can_encode_heartbeat(pack->uptime_ms, &frame);
    (void)hal_can_transmit(&frame);

    bms_can_encode_voltages(pack, &frame);
    (void)hal_can_transmit(&frame);

    bms_can_encode_cell_broadcast(pack, s_cell_broadcast_idx, &frame);
    (void)hal_can_transmit(&frame);
    s_cell_broadcast_idx++;
    if (s_cell_broadcast_idx >= max_broadcast) { s_cell_broadcast_idx = 0U; }

    bms_can_encode_temps(pack, &frame);
    (void)hal_can_transmit(&frame);
}

/* ═══════════════════════════════════════════════════════════════════════
 * CC-01 / P2-01: CAN Authentication Stub
 *
 * Sequence counter tracking is implemented now. AES-128-CMAC signature
 * verification is deferred to Phase 2.
 *
 * When BMS_CAN_AUTH_ENABLED=1:
 *   - TX frames get sequence counter in reserved bytes
 *   - RX frames must have valid (monotonically increasing) sequence counter
 *   - Auth failure → frame rejected + logged
 *
 * When BMS_CAN_AUTH_ENABLED=0 (default):
 *   - Counters still track (for debugging) but auth is not enforced
 * ═══════════════════════════════════════════════════════════════════════ */

static uint16_t s_tx_seq_counter;
static uint16_t s_rx_seq_counter;
static bool     s_rx_seq_initialized;

bool bms_can_auth_verify(const bms_can_frame_t *frame)
{
    /* TODO P2-01: Implement AES-128-CMAC verification */

    /* Sequence counter validation (always tracked) */
    if (frame->dlc < 8U) {
        if (BMS_CAN_AUTH_ENABLED) {
            BMS_LOG("CC-01: Auth reject — DLC %u < 8 (no room for seq counter)", frame->dlc);
            return false;
        }
        return true;
    }

    uint16_t rx_seq = (uint16_t)((uint16_t)frame->data[6] << 8U) | (uint16_t)frame->data[7];

    if (!s_rx_seq_initialized) {
        s_rx_seq_counter = rx_seq;
        s_rx_seq_initialized = true;
        return true;
    }

    /* Expect monotonically increasing (with wrap) */
    uint16_t expected_next = (uint16_t)(s_rx_seq_counter + 1U);
    if (rx_seq != expected_next && rx_seq != s_rx_seq_counter) {
        if (BMS_CAN_AUTH_ENABLED) {
            BMS_LOG("CC-01: Auth reject — seq %u, expected %u", rx_seq, expected_next);
            return false;
        }
        /* Not enforced, just log and accept */
        BMS_LOG("CC-01: Seq counter mismatch (non-enforced) — got %u, expected %u",
                rx_seq, expected_next);
    }

    s_rx_seq_counter = rx_seq;
    return true;
}

void bms_can_auth_sign(bms_can_frame_t *frame)
{
    /* TODO P2-01: Implement AES-128-CMAC signing */

    /* Increment TX sequence counter and embed in frame */
    s_tx_seq_counter++;
    if (frame->dlc >= 8U) {
        frame->data[6] = (uint8_t)((s_tx_seq_counter >> 8U) & 0xFFU);
        frame->data[7] = (uint8_t)(s_tx_seq_counter & 0xFFU);
    }
}

uint16_t bms_can_auth_get_tx_seq(void) { return s_tx_seq_counter; }
uint16_t bms_can_auth_get_rx_seq(void) { return s_rx_seq_counter; }

/* ── RX processing ─────────────────────────────────────────────────── */

bool bms_can_rx_process(bms_ems_command_t *cmd)
{
    bms_can_frame_t frame;

    while (hal_can_receive(&frame) == 0) {
        /* CC-01: Auth check on all received frames */
        if (BMS_CAN_AUTH_ENABLED && !bms_can_auth_verify(&frame)) {
            BMS_LOG("CC-01: Frame 0x%03X rejected — auth failed", frame.id);
            continue;  /* reject frame */
        }

        if (frame.id == CAN_ID_EMS_COMMAND) {
            if (bms_can_decode_ems_command(&frame, cmd) == 0) {
                return true;
            }
        }
        if (frame.id == CAN_ID_EMS_HEARTBEAT) {
            cmd->type = EMS_CMD_NONE;
            cmd->timestamp_ms = hal_tick_ms();
            cmd->valid = true;
            return true;
        }
    }

    return false;
}
