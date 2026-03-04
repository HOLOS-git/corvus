/**
 * @file bms_contactor.h
 * @brief Contactor control with bus voltage pre-charge
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P0-03: Pre-charge uses BUS voltage, not pack voltage (Dave CRITICAL)
 *   CC-10: ADC_BUS_VOLTAGE defined but never used — now used (Dave, Mikael, Henrik)
 */

#ifndef BMS_CONTACTOR_H
#define BMS_CONTACTOR_H

#include "bms_types.h"

typedef struct {
    bms_contactor_state_t state;
    uint32_t              state_timer_ms;
    uint32_t              target_bus_voltage_mv;  /* P0-03: target from bus ADC */
    bool                  close_requested;
    bool                  open_requested;
} bms_contactor_ctx_t;

void bms_contactor_init(bms_contactor_ctx_t *ctx);

/**
 * P0-03: Request close using ACTUAL bus voltage from ADC.
 * Voltage match: |pack - bus| < BMS_VOLTAGE_MATCH_MV before main close.
 */
void bms_contactor_request_close(bms_contactor_ctx_t *ctx,
                                  uint32_t bus_voltage_mv);

void bms_contactor_request_open(bms_contactor_ctx_t *ctx);

void bms_contactor_run(bms_contactor_ctx_t *ctx,
                        bms_pack_data_t *pack,
                        uint32_t dt_ms);

bms_contactor_state_t bms_contactor_get_state(const bms_contactor_ctx_t *ctx);
bool bms_contactor_is_faulted(const bms_contactor_ctx_t *ctx);

#endif /* BMS_CONTACTOR_H */
