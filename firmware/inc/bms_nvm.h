/**
 * bms_nvm.h — Non-volatile memory fault logging and persistence
 *
 * HAL-abstracted NVM: ring buffer of fault events, persistent SoC,
 * runtime counters.
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_NVM_H
#define BMS_NVM_H

#include <stdint.h>
#include <stdbool.h>

/* ── Fault event structure ─────────────────────────────────────────── */
#define BMS_NVM_FAULT_LOG_SIZE  64U

typedef struct {
    uint32_t timestamp_ms;
    uint8_t  fault_type;    /* bitmask or enum of fault */
    uint8_t  cell_index;    /* relevant cell (0xFF if N/A) */
    uint16_t value;         /* relevant value (mV, deci-°C, etc.) */
} bms_nvm_fault_event_t;

/* ── Persistent data structure ─────────────────────────────────────── */
typedef struct {
    uint16_t soc_hundredths;      /* SoC at last shutdown */
    uint32_t runtime_hours;       /* total runtime hours */
    uint32_t total_charge_mah;    /* total charge Ah (in mAh) */
    uint32_t total_discharge_mah; /* total discharge Ah (in mAh) */
} bms_nvm_persistent_t;

/* ── NVM context ───────────────────────────────────────────────────── */
typedef struct {
    bms_nvm_fault_event_t fault_log[BMS_NVM_FAULT_LOG_SIZE];
    uint8_t               fault_head;   /* next write index */
    uint8_t               fault_count;  /* entries used (max 64) */
    bms_nvm_persistent_t  persistent;
} bms_nvm_ctx_t;

/* ── HAL functions ─────────────────────────────────────────────────── */
void bms_hal_nvm_write(uint32_t addr, const void *data, uint16_t len);
void bms_hal_nvm_read(uint32_t addr, void *data, uint16_t len);

/* ── API ───────────────────────────────────────────────────────────── */

/**
 * Initialize NVM subsystem. Loads persistent data from NVM.
 */
void bms_nvm_init(bms_nvm_ctx_t *ctx);

/**
 * Log a fault event to the ring buffer.
 */
void bms_nvm_log_fault(bms_nvm_ctx_t *ctx,
                        uint32_t timestamp_ms,
                        uint8_t fault_type,
                        uint8_t cell_index,
                        uint16_t value);

/**
 * Save persistent data (SoC, runtime, Ah) to NVM.
 * Call on shutdown or periodically.
 */
void bms_nvm_save_persistent(bms_nvm_ctx_t *ctx);

/**
 * Load persistent data from NVM.
 */
void bms_nvm_load_persistent(bms_nvm_ctx_t *ctx);

/**
 * Get fault event from ring buffer.
 * @param ctx    NVM context
 * @param idx    index from most recent (0 = newest)
 * @param event  output event
 * @return true if valid event at index
 */
bool bms_nvm_get_fault(const bms_nvm_ctx_t *ctx,
                        uint8_t idx,
                        bms_nvm_fault_event_t *event);

#endif /* BMS_NVM_H */
