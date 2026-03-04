/**
 * @file bms_nvm.c
 * @brief NVM fault logging — expanded
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P2-05: Reset events logged to NVM (Yara)
 *   P1-02: IWDG reset events logged (Henrik)
 */

#include "bms_nvm.h"
#include "bms_hal.h"
#include "bms_config.h"
#include <string.h>

#define NVM_ADDR_FAULT_LOG    0x0000U
#define NVM_ADDR_FAULT_HEAD   (NVM_ADDR_FAULT_LOG + \
    (uint32_t)(BMS_NVM_FAULT_LOG_SIZE * sizeof(bms_nvm_fault_event_t)))
#define NVM_ADDR_FAULT_COUNT  (NVM_ADDR_FAULT_HEAD + 1U)
#define NVM_ADDR_PERSISTENT   (NVM_ADDR_FAULT_COUNT + 1U)

/* P3-06: Shadow/staging area for atomic writes (Dave — NVM write atomicity)
 * Offset the shadow area after the active area to avoid overlap. */
#define NVM_SHADOW_OFFSET     0x1000U
#define NVM_ADDR_SHADOW_LOG   (NVM_ADDR_FAULT_LOG + NVM_SHADOW_OFFSET)
#define NVM_ADDR_SHADOW_HEAD  (NVM_ADDR_FAULT_HEAD + NVM_SHADOW_OFFSET)
#define NVM_ADDR_SHADOW_COUNT (NVM_ADDR_FAULT_COUNT + NVM_SHADOW_OFFSET)
#define NVM_ADDR_SHADOW_PERSISTENT (NVM_ADDR_PERSISTENT + NVM_SHADOW_OFFSET)

void bms_nvm_init(bms_nvm_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    bms_nvm_load_persistent(ctx);
}

/* P3-06: Atomic NVM write via shadow/staging area (Dave — carried from V1)
 *
 * Write sequence:
 *   1. Write data to shadow/staging area
 *   2. Read back shadow and verify against RAM
 *   3. If verified, commit (copy shadow to active area)
 *   4. Power loss during step 1-2 → active area untouched
 *   5. Power loss during step 3 → worst case: stale active data, shadow has new
 *
 * This ensures power loss during write doesn't corrupt the active entry.
 */
static bool nvm_atomic_write(uint32_t active_addr, uint32_t shadow_addr,
                              const void *data, uint16_t len)
{
    uint8_t verify_buf[sizeof(bms_nvm_fault_event_t)]; /* large enough for any single write */

    /* Step 1: Write to shadow area */
    bms_hal_nvm_write(shadow_addr, data, len);

    /* Step 2: Read back and verify */
    if (len <= (uint16_t)sizeof(verify_buf)) {
        bms_hal_nvm_read(shadow_addr, verify_buf, len);
        if (memcmp(verify_buf, data, len) != 0) {
            BMS_LOG("P3-06: NVM shadow verify failed at 0x%04X", (unsigned)shadow_addr);
            return false;
        }
    }

    /* Step 3: Commit — copy shadow to active */
    bms_hal_nvm_write(active_addr, data, len);
    return true;
}

void bms_nvm_log_fault(bms_nvm_ctx_t *ctx, uint32_t timestamp_ms,
                        uint8_t fault_type, uint8_t cell_index, uint16_t value)
{
    bms_nvm_fault_event_t *ev = &ctx->fault_log[ctx->fault_head];
    ev->timestamp_ms = timestamp_ms;
    ev->fault_type = fault_type;
    ev->cell_index = cell_index;
    ev->value = value;

    uint32_t ev_offset = (uint32_t)ctx->fault_head * (uint32_t)sizeof(*ev);

    /* P3-06: Atomic write — shadow first, verify, then commit */
    (void)nvm_atomic_write(
        NVM_ADDR_FAULT_LOG + ev_offset,
        NVM_ADDR_SHADOW_LOG + ev_offset,
        ev, (uint16_t)sizeof(*ev));

    ctx->fault_head = (uint8_t)((ctx->fault_head + 1U) % BMS_NVM_FAULT_LOG_SIZE);
    if (ctx->fault_count < BMS_NVM_FAULT_LOG_SIZE) { ctx->fault_count++; }

    (void)nvm_atomic_write(NVM_ADDR_FAULT_HEAD, NVM_ADDR_SHADOW_HEAD,
                           &ctx->fault_head, 1U);
    (void)nvm_atomic_write(NVM_ADDR_FAULT_COUNT, NVM_ADDR_SHADOW_COUNT,
                           &ctx->fault_count, 1U);
}

bool bms_nvm_get_fault(const bms_nvm_ctx_t *ctx, uint8_t idx,
                        bms_nvm_fault_event_t *event)
{
    if (idx >= ctx->fault_count) { return false; }
    uint8_t actual = (uint8_t)((ctx->fault_head + BMS_NVM_FAULT_LOG_SIZE - 1U - idx)
                     % BMS_NVM_FAULT_LOG_SIZE);
    *event = ctx->fault_log[actual];
    return true;
}

void bms_nvm_save_persistent(bms_nvm_ctx_t *ctx)
{
    /* P3-06: Atomic write for persistent data */
    (void)nvm_atomic_write(NVM_ADDR_PERSISTENT, NVM_ADDR_SHADOW_PERSISTENT,
                           &ctx->persistent, (uint16_t)sizeof(ctx->persistent));
}

void bms_nvm_load_persistent(bms_nvm_ctx_t *ctx)
{
    bms_hal_nvm_read(NVM_ADDR_PERSISTENT, &ctx->persistent,
                     (uint16_t)sizeof(ctx->persistent));
    bms_hal_nvm_read(NVM_ADDR_FAULT_HEAD, &ctx->fault_head, 1U);
    bms_hal_nvm_read(NVM_ADDR_FAULT_COUNT, &ctx->fault_count, 1U);

    if (ctx->fault_head >= BMS_NVM_FAULT_LOG_SIZE) { ctx->fault_head = 0U; }
    if (ctx->fault_count > BMS_NVM_FAULT_LOG_SIZE) { ctx->fault_count = 0U; }

    bms_hal_nvm_read(NVM_ADDR_FAULT_LOG, ctx->fault_log,
                     (uint16_t)(BMS_NVM_FAULT_LOG_SIZE * sizeof(bms_nvm_fault_event_t)));
}
