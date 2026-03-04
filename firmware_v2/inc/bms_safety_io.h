/**
 * @file bms_safety_io.h
 * @brief Gas/Ventilation/Fire/IMD safety interfaces
 *
 * Street Smart Edition. NEW file — not in original firmware.
 * Reviewer findings addressed:
 *   P1-03: Gas detection (Catherine CRITICAL, Mikael, Henrik SHOWSTOPPER, Priya)
 *   P1-04: Ventilation monitoring (Henrik SHOWSTOPPER, Priya)
 *   P1-05: Fire detection/suppression (Henrik MAJOR, Priya CRITICAL)
 *   P1-06: Insulation monitoring (Dave CRITICAL, Catherine, Mikael CRITICAL, Henrik)
 */

#ifndef BMS_SAFETY_IO_H
#define BMS_SAFETY_IO_H

#include "bms_types.h"
#include "bms_nvm.h"

/**
 * Set NVM context for IMD resistance trend logging.
 */
void bms_safety_io_set_nvm(bms_nvm_ctx_t *nvm);

/**
 * Initialize safety I/O subsystem.
 */
void bms_safety_io_init(bms_safety_io_state_t *sio);

/**
 * Run safety I/O polling. Call at BMS_SAFETY_IO_PERIOD_MS.
 * Reads GPIO inputs, updates state, sets fault flags.
 *
 * P1-03: Gas high alarm → contactors open + fault latch within 2s
 * P1-04: Vent failure → alarm + load reduction + inhibit close
 * P1-05: Fire → disconnect + post-incident interlock (manual reset)
 * P1-06: IMD alarm → pack disconnect (non-optional)
 *
 * @param sio   safety I/O state
 * @param pack  pack data (writes fault flags)
 */
void bms_safety_io_run(bms_safety_io_state_t *sio,
                       bms_pack_data_t *pack);

/**
 * Check if contactor closure should be inhibited.
 * Returns true if vent failure or other safety condition prevents closing.
 */
bool bms_safety_io_inhibit_close(const bms_safety_io_state_t *sio);

/**
 * Check if any safety I/O condition requires emergency shutdown.
 */
bool bms_safety_io_emergency_shutdown(const bms_safety_io_state_t *sio);

/**
 * Encode safety I/O status into CAN frame (ID 0x150).
 */
void bms_safety_io_encode_can(const bms_safety_io_state_t *sio,
                               bms_can_frame_t *frame);

#endif /* BMS_SAFETY_IO_H */
