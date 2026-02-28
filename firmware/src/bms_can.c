/**
 * bms_can.c — CAN TX/RX with message framing per Orca Modbus register map
 *
 * Simplified demo protocol — not J1939 or Modbus-over-CAN.
 *
 * CAN 2.0B standard frame format.
 * Message IDs mapped from Orca Modbus TCP register groups (Appendix A):
 *   0x100: Array status (regs 0–25)
 *   0x105: Current limits + SoC
 *   0x108: Heartbeat
 *   0x110: Pack status (regs 50–97)
 *   0x120: Alarms (regs 400+)
 *   0x130: Cell voltage summary
 *   0x131+: Cell voltage broadcast
 *   0x140: Temperatures + current limits
 *   0x200: EMS commands (regs 300–343)
 *   0x210: EMS heartbeat
 *
 * All multi-byte values: big-endian (network byte order) in CAN frames.
 *
 * Reference: Orca ESS Integrator Manual §8.2, Appendix A
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include "bms_can.h"
#include "bms_hal.h"
#include "bms_config.h"
#include <string.h>

/* ── Big-endian pack helpers ───────────────────────────────────────── */

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

static uint16_t unpack_u16_be(const uint8_t *buf)
{
    return (uint16_t)((uint16_t)buf[0] << 8U) | (uint16_t)buf[1];
}

static int16_t unpack_i16_be(const uint8_t *buf)
{
    return (int16_t)unpack_u16_be(buf);
}

/* ── Init ──────────────────────────────────────────────────────────── */

void bms_can_init(void)
{
    /* HAL CAN init handled by hal_init() */
}

/* ── Encode pack status (0x100) ────────────────────────────────────── */

void bms_can_encode_status(const bms_pack_data_t *pack,
                            bms_can_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->id = CAN_ID_ARRAY_STATUS;
    frame->dlc = 8U;

    /* [0] pack mode */
    frame->data[0] = (uint8_t)pack->mode;

    /* [1:2] pack voltage in 0.1V = pack_voltage_mv / 100 */
    pack_u16_be(&frame->data[1], (uint16_t)(pack->pack_voltage_mv / 100U));

    /* [3:4] pack current in 0.1A = pack_current_ma / 100 */
    pack_i16_be(&frame->data[3], (int16_t)(pack->pack_current_ma / 100));

    /* [5] SoC % (0–100) */
    frame->data[5] = (uint8_t)(pack->soc_hundredths / 100U);

    /* [6] max temp °C with +40 offset (range: -40..+215°C) */
    {
        int16_t temp_c = pack->max_temp_deci_c / 10;
        frame->data[6] = (uint8_t)(temp_c + 40);
    }

    /* [7] fault flags low byte */
    {
        uint32_t f;
        memcpy(&f, &pack->faults, sizeof(f));
        frame->data[7] = (uint8_t)(f & 0xFFU);
    }
}

/* ── Encode cell voltages (0x130) ──────────────────────────────────── */

void bms_can_encode_voltages(const bms_pack_data_t *pack,
                              bms_can_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->id = CAN_ID_PACK_VOLTAGES;
    frame->dlc = 8U;

    pack_u16_be(&frame->data[0], pack->max_cell_mv);
    pack_u16_be(&frame->data[2], pack->min_cell_mv);
    pack_u16_be(&frame->data[4], pack->avg_cell_mv);
    pack_u16_be(&frame->data[6], (uint16_t)(pack->max_cell_mv - pack->min_cell_mv));
}

/* ── Encode temperatures + limits (0x140) ──────────────────────────── */

void bms_can_encode_temps(const bms_pack_data_t *pack,
                           bms_can_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->id = CAN_ID_PACK_TEMPS;
    frame->dlc = 8U;

    pack_i16_be(&frame->data[0], pack->max_temp_deci_c);
    pack_i16_be(&frame->data[2], pack->min_temp_deci_c);
    /* Current limits in 0.1A */
    pack_i16_be(&frame->data[4], (int16_t)(pack->charge_limit_ma / 100));
    pack_i16_be(&frame->data[6], (int16_t)(pack->discharge_limit_ma / 100));
}

/* ── Decode EMS command (0x200) ────────────────────────────────────── */

int32_t bms_can_decode_ems_command(const bms_can_frame_t *frame,
                                    bms_ems_command_t *cmd)
{
    if (frame->id != CAN_ID_EMS_COMMAND || frame->dlc < 5U) {
        return -1;
    }

    cmd->type = (bms_ems_cmd_type_t)frame->data[0];
    cmd->charge_limit_ma = (int32_t)unpack_i16_be(&frame->data[1]) * 1000;
    cmd->discharge_limit_ma = (int32_t)unpack_i16_be(&frame->data[3]) * 1000;
    cmd->timestamp_ms = hal_tick_ms();

    /* Validate command type */
    if (cmd->type > EMS_CMD_SET_LIMITS) {
        return -1;
    }

    return 0;
}

/* ── Encode heartbeat ──────────────────────────────────────────────── */

void bms_can_encode_heartbeat(uint32_t uptime_ms, bms_can_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->id = 0x108U;  /* dedicated heartbeat CAN ID */
    frame->dlc = 8U;
    pack_u32_be(&frame->data[0], uptime_ms);
}

/* ── Encode current limits (0x105) ─────────────────────────────────── */

void bms_can_encode_limits(const bms_pack_data_t *pack,
                            bms_can_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->id = 0x105U;
    frame->dlc = 8U;
    pack_u32_be(&frame->data[0], (uint32_t)pack->charge_limit_ma);
    pack_u32_be(&frame->data[4], (uint32_t)pack->discharge_limit_ma);
}

/* ── Encode cell voltage broadcast (0x131+) ────────────────────────── */

void bms_can_encode_cell_broadcast(const bms_pack_data_t *pack,
                                    uint8_t frame_idx,
                                    bms_can_frame_t *frame)
{
    uint16_t base;
    uint8_t i;

    memset(frame, 0, sizeof(*frame));
    frame->id = 0x131U + (uint32_t)frame_idx;
    frame->dlc = 8U;

    base = (uint16_t)frame_idx * 4U;
    for (i = 0U; i < 4U; i++) {
        uint16_t idx = base + i;
        uint16_t mv = 0U;
        if (idx < BMS_SE_PER_PACK) {
            mv = pack->cell_mv[idx];
        }
        pack_u16_be(&frame->data[i * 2U], mv);
    }
}

/* ── Periodic TX ───────────────────────────────────────────────────── */

/* Cell voltage broadcast cycling state */
static uint8_t s_cell_broadcast_idx;

void bms_can_tx_periodic(const bms_pack_data_t *pack)
{
    bms_can_frame_t frame;
    /* Total broadcast frames needed: ceil(308/4) = 77 */
    uint8_t max_broadcast_idx = (uint8_t)((BMS_SE_PER_PACK + 3U) / 4U);

    /* Status frame */
    bms_can_encode_status(pack, &frame);
    hal_can_transmit(&frame);

    /* Current limits frame */
    bms_can_encode_limits(pack, &frame);
    hal_can_transmit(&frame);

    /* Heartbeat frame */
    bms_can_encode_heartbeat(pack->uptime_ms, &frame);
    hal_can_transmit(&frame);

    /* Voltage summary frame */
    bms_can_encode_voltages(pack, &frame);
    hal_can_transmit(&frame);

    /* Cell voltage broadcast — one module's worth per cycle (4 cells) */
    bms_can_encode_cell_broadcast(pack, s_cell_broadcast_idx, &frame);
    hal_can_transmit(&frame);
    s_cell_broadcast_idx++;
    if (s_cell_broadcast_idx >= max_broadcast_idx) {
        s_cell_broadcast_idx = 0U;
    }

    /* Temperature + limits frame */
    bms_can_encode_temps(pack, &frame);
    hal_can_transmit(&frame);
}

/* ── RX processing ─────────────────────────────────────────────────── */

bool bms_can_rx_process(bms_ems_command_t *cmd)
{
    bms_can_frame_t frame;

    while (hal_can_receive(&frame) == 0) {
        if (frame.id == CAN_ID_EMS_COMMAND) {
            if (bms_can_decode_ems_command(&frame, cmd) == 0) {
                return true;
            }
        }
        /* EMS heartbeat updates watchdog (handled by caller) */
        if (frame.id == CAN_ID_EMS_HEARTBEAT) {
            cmd->type = EMS_CMD_NONE;
            cmd->timestamp_ms = hal_tick_ms();
            return true;
        }
    }

    return false;
}
