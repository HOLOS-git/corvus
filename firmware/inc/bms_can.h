/**
 * bms_can.h — CAN driver + message framing per Orca Modbus register map
 *
 * CAN 2.0B standard frame format.
 * Message IDs mapped from Orca Modbus TCP register groups:
 *   0x100: Array status (regs 0–25)
 *   0x110: Pack status (regs 50–97)
 *   0x120: Pack alarms (regs 400+)
 *   0x200: EMS commands (regs 300–343)
 *
 * Reference: Orca ESS Integrator Manual §8.2, Appendix A
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_CAN_H
#define BMS_CAN_H

#include "bms_types.h"

/**
 * Initialize CAN subsystem.
 */
void bms_can_init(void);

/**
 * Encode pack status into CAN frame (ID 0x100).
 * Byte layout:
 *   [0]    pack mode (uint8)
 *   [1:2]  pack voltage / 100 (uint16 BE, units: 0.1V)
 *   [3:4]  pack current / 100 (int16 BE, units: 0.1A)
 *   [5]    SoC % (uint8, 0–100)
 *   [6]    max temp °C (int8, offset +40)
 *   [7]    fault flags low byte
 */
void bms_can_encode_status(const bms_pack_data_t *pack,
                            bms_can_frame_t *frame);

/**
 * Encode cell voltage summary into CAN frame (ID 0x130).
 * Byte layout:
 *   [0:1]  max cell mV (uint16 BE)
 *   [2:3]  min cell mV (uint16 BE)
 *   [4:5]  avg cell mV (uint16 BE)
 *   [6:7]  cell imbalance mV (uint16 BE)
 */
void bms_can_encode_voltages(const bms_pack_data_t *pack,
                              bms_can_frame_t *frame);

/**
 * Encode temperature summary into CAN frame (ID 0x140).
 * Byte layout:
 *   [0:1]  max temp (int16 BE, 0.1°C)
 *   [2:3]  min temp (int16 BE, 0.1°C)
 *   [4:5]  charge limit / 100 (int16 BE, units: 0.1A)
 *   [6:7]  discharge limit / 100 (int16 BE, units: 0.1A)
 */
void bms_can_encode_temps(const bms_pack_data_t *pack,
                           bms_can_frame_t *frame);

/**
 * Decode EMS command from CAN frame (ID 0x200).
 * Byte layout:
 *   [0]    command type (bms_ems_cmd_type_t)
 *   [1:2]  charge limit (int16 BE, units: 1A, for SET_LIMITS)
 *   [3:4]  discharge limit (int16 BE, units: 1A, for SET_LIMITS)
 *   [5:7]  reserved
 *
 * @return 0 on success, negative on invalid frame
 */
int32_t bms_can_decode_ems_command(const bms_can_frame_t *frame,
                                    bms_ems_command_t *cmd);

/**
 * Encode heartbeat frame (ID 0x100 + offset).
 * Byte layout:
 *   [0:3]  uptime_ms (uint32 BE)
 *   [4:7]  reserved
 */
void bms_can_encode_heartbeat(uint32_t uptime_ms, bms_can_frame_t *frame);

/**
 * Transmit all periodic status frames. Called from CAN TX task.
 */
void bms_can_tx_periodic(const bms_pack_data_t *pack);

/**
 * Process received CAN frames. Called from CAN RX task.
 * @param cmd  output: decoded EMS command (if any)
 * @return true if a valid EMS command was received
 */
bool bms_can_rx_process(bms_ems_command_t *cmd);

#endif /* BMS_CAN_H */
