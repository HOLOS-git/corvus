/**
 * bms_contactor.h — Contactor state machine with welding detection
 *
 * States: OPEN → PRE_CHARGE → CLOSING → CLOSED → OPENING → WELDED_FAULT
 * GPIO-based: pre-charge relay, main positive, main negative
 * Feedback verification via ADC voltage across contactor
 * Welding detection: after open command, verify current ceases
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_CONTACTOR_H
#define BMS_CONTACTOR_H

#include "bms_types.h"

/* ── Contactor subsystem state ─────────────────────────────────────── */
typedef struct {
    bms_contactor_state_t state;
    uint32_t              state_timer_ms;    /* time in current state    */
    uint32_t              bus_voltage_mv;    /* last known bus voltage   */
    bool                  close_requested;
    bool                  open_requested;
} bms_contactor_ctx_t;

/**
 * Initialize contactor subsystem — all contactors open.
 */
void bms_contactor_init(bms_contactor_ctx_t *ctx);

/**
 * Request contactor close sequence (OPEN → PRE_CHARGE → CLOSING → CLOSED).
 * @param bus_voltage_mv  current bus voltage for pre-charge target
 */
void bms_contactor_request_close(bms_contactor_ctx_t *ctx,
                                  uint32_t bus_voltage_mv);

/**
 * Request contactor open sequence (CLOSED → OPENING → OPEN or WELDED).
 */
void bms_contactor_request_open(bms_contactor_ctx_t *ctx);

/**
 * Run contactor state machine — called every 50ms.
 * @param ctx      contactor context
 * @param pack     pack data (reads pack_voltage_mv, pack_current_ma)
 * @param dt_ms    time since last call
 */
void bms_contactor_run(bms_contactor_ctx_t *ctx,
                        bms_pack_data_t *pack,
                        uint32_t dt_ms);

/**
 * Get current contactor state.
 */
bms_contactor_state_t bms_contactor_get_state(const bms_contactor_ctx_t *ctx);

/**
 * Check if contactor is in a fault state.
 */
bool bms_contactor_is_faulted(const bms_contactor_ctx_t *ctx);

#endif /* BMS_CONTACTOR_H */
