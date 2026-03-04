/**
 * @file bms_nvm.h
 * @brief NVM fault logging — expanded
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P2-05: Reset event logging (Yara)
 *   P1-02: IWDG reset logging (Henrik)
 */

#ifndef BMS_NVM_H
#define BMS_NVM_H

#include <stdint.h>
#include <stdbool.h>
#include "bms_config.h"

/* Fault types for NVM log */
typedef enum {
    NVM_FAULT_OV          = 1,
    NVM_FAULT_UV          = 2,
    NVM_FAULT_OT          = 3,
    NVM_FAULT_OC_CHG      = 4,
    NVM_FAULT_OC_DCHG     = 5,
    NVM_FAULT_COMM_LOSS   = 6,
    NVM_FAULT_SENSOR      = 7,
    NVM_FAULT_DTDT        = 8,
    NVM_FAULT_SUBZERO     = 9,
    NVM_FAULT_GAS         = 10,
    NVM_FAULT_FIRE        = 11,
    NVM_FAULT_IMD         = 12,
    NVM_FAULT_VENT        = 13,
    NVM_FAULT_WELD        = 14,
    NVM_FAULT_PLAUSIBILITY= 15,
    NVM_FAULT_RESET       = 16,  /* P2-05: fault reset event */
    NVM_FAULT_IWDG        = 17,  /* P1-02: IWDG reset event */
    NVM_FAULT_HW_OV       = 18,
    NVM_FAULT_HW_UV       = 19,
    NVM_FAULT_HW_OT       = 20,
    NVM_FAULT_IMD_TREND   = 21   /* P1-06: periodic resistance log entry */
} bms_nvm_fault_type_t;

typedef struct {
    uint32_t timestamp_ms;
    uint8_t  fault_type;
    uint8_t  cell_index;    /* 0xFF if N/A */
    uint16_t value;
} bms_nvm_fault_event_t;

typedef struct {
    uint16_t soc_hundredths;
    uint32_t runtime_hours;
    uint32_t total_charge_mah;
    uint32_t total_discharge_mah;
} bms_nvm_persistent_t;

typedef struct {
    bms_nvm_fault_event_t fault_log[BMS_NVM_FAULT_LOG_SIZE];
    uint8_t               fault_head;
    uint8_t               fault_count;
    bms_nvm_persistent_t  persistent;
} bms_nvm_ctx_t;

void bms_nvm_init(bms_nvm_ctx_t *ctx);
void bms_nvm_log_fault(bms_nvm_ctx_t *ctx, uint32_t timestamp_ms,
                        uint8_t fault_type, uint8_t cell_index, uint16_t value);
void bms_nvm_save_persistent(bms_nvm_ctx_t *ctx);
void bms_nvm_load_persistent(bms_nvm_ctx_t *ctx);
bool bms_nvm_get_fault(const bms_nvm_ctx_t *ctx, uint8_t idx,
                        bms_nvm_fault_event_t *event);

#endif /* BMS_NVM_H */
