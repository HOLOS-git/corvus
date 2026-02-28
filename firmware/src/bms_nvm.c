/**
 * bms_nvm.c — Non-volatile memory fault logging and persistence
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include "bms_nvm.h"
#include <string.h>

/* ── NVM memory layout addresses ───────────────────────────────────── */
#define NVM_ADDR_FAULT_LOG       0x0000U
#define NVM_ADDR_FAULT_HEAD      (NVM_ADDR_FAULT_LOG + \
    (uint32_t)(BMS_NVM_FAULT_LOG_SIZE * sizeof(bms_nvm_fault_event_t)))
#define NVM_ADDR_FAULT_COUNT     (NVM_ADDR_FAULT_HEAD + 1U)
#define NVM_ADDR_PERSISTENT      (NVM_ADDR_FAULT_COUNT + 1U)

/* ── Init ──────────────────────────────────────────────────────────── */

void bms_nvm_init(bms_nvm_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    bms_nvm_load_persistent(ctx);
}

/* ── Log fault ─────────────────────────────────────────────────────── */

void bms_nvm_log_fault(bms_nvm_ctx_t *ctx,
                        uint32_t timestamp_ms,
                        uint8_t fault_type,
                        uint8_t cell_index,
                        uint16_t value)
{
    bms_nvm_fault_event_t *ev = &ctx->fault_log[ctx->fault_head];

    ev->timestamp_ms = timestamp_ms;
    ev->fault_type = fault_type;
    ev->cell_index = cell_index;
    ev->value = value;

    /* Write event to NVM */
    bms_hal_nvm_write(
        NVM_ADDR_FAULT_LOG + (uint32_t)ctx->fault_head * (uint32_t)sizeof(*ev),
        ev, (uint16_t)sizeof(*ev));

    /* Advance head */
    ctx->fault_head = (uint8_t)((ctx->fault_head + 1U) % BMS_NVM_FAULT_LOG_SIZE);
    if (ctx->fault_count < BMS_NVM_FAULT_LOG_SIZE) {
        ctx->fault_count++;
    }

    /* Write head and count */
    bms_hal_nvm_write(NVM_ADDR_FAULT_HEAD, &ctx->fault_head, 1U);
    bms_hal_nvm_write(NVM_ADDR_FAULT_COUNT, &ctx->fault_count, 1U);
}

/* ── Get fault from ring buffer ────────────────────────────────────── */

bool bms_nvm_get_fault(const bms_nvm_ctx_t *ctx,
                        uint8_t idx,
                        bms_nvm_fault_event_t *event)
{
    uint8_t actual_idx;

    if (idx >= ctx->fault_count) {
        return false;
    }

    /* idx=0 is most recent = (head - 1), idx=1 = (head - 2), etc. */
    actual_idx = (uint8_t)((ctx->fault_head + BMS_NVM_FAULT_LOG_SIZE - 1U - idx)
                  % BMS_NVM_FAULT_LOG_SIZE);
    *event = ctx->fault_log[actual_idx];
    return true;
}

/* ── Persistent data ───────────────────────────────────────────────── */

void bms_nvm_save_persistent(bms_nvm_ctx_t *ctx)
{
    bms_hal_nvm_write(NVM_ADDR_PERSISTENT, &ctx->persistent,
                      (uint16_t)sizeof(ctx->persistent));
}

void bms_nvm_load_persistent(bms_nvm_ctx_t *ctx)
{
    bms_hal_nvm_read(NVM_ADDR_PERSISTENT, &ctx->persistent,
                     (uint16_t)sizeof(ctx->persistent));

    /* Also load fault ring buffer metadata */
    bms_hal_nvm_read(NVM_ADDR_FAULT_HEAD, &ctx->fault_head, 1U);
    bms_hal_nvm_read(NVM_ADDR_FAULT_COUNT, &ctx->fault_count, 1U);

    /* Validate */
    if (ctx->fault_head >= BMS_NVM_FAULT_LOG_SIZE) {
        ctx->fault_head = 0U;
    }
    if (ctx->fault_count > BMS_NVM_FAULT_LOG_SIZE) {
        ctx->fault_count = 0U;
    }

    /* Load fault events */
    bms_hal_nvm_read(NVM_ADDR_FAULT_LOG, ctx->fault_log,
                     (uint16_t)(BMS_NVM_FAULT_LOG_SIZE * sizeof(bms_nvm_fault_event_t)));
}

/* ── HAL mock implementation ───────────────────────────────────────── */

#ifdef DESKTOP_BUILD

#define MOCK_NVM_SIZE 4096U
static uint8_t s_mock_nvm[MOCK_NVM_SIZE];

void bms_hal_nvm_write(uint32_t addr, const void *data, uint16_t len)
{
    if (addr + len <= MOCK_NVM_SIZE) {
        memcpy(&s_mock_nvm[addr], data, len);
    }
}

void bms_hal_nvm_read(uint32_t addr, void *data, uint16_t len)
{
    if (addr + len <= MOCK_NVM_SIZE) {
        memcpy(data, &s_mock_nvm[addr], len);
    } else {
        memset(data, 0, len);
    }
}

void mock_nvm_reset(void)
{
    memset(s_mock_nvm, 0, sizeof(s_mock_nvm));
}

#endif /* DESKTOP_BUILD */
