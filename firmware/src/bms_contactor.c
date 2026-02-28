/**
 * bms_contactor.c — Contactor state machine with welding detection
 *
 * State transitions:
 *   OPEN → PRE_CHARGE → CLOSING → CLOSED → OPENING → OPEN
 *                                                   → WELDED_FAULT
 *
 * Pre-charge: close pre-charge relay + main negative.
 *   Wait until pack voltage reaches 95% of bus voltage, or timeout.
 * Close: close main positive, verify feedback.
 * Open: open all contactors, verify current drops to near-zero.
 * Weld detect: if current persists after open → WELDED_FAULT.
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include "bms_contactor.h"
#include "bms_hal.h"
#include "bms_config.h"

/* ── All contactors off ────────────────────────────────────────────── */
static void all_contactors_off(void)
{
    hal_gpio_write(GPIO_CONTACTOR_POS, false);
    hal_gpio_write(GPIO_CONTACTOR_NEG, false);
    hal_gpio_write(GPIO_PRECHARGE_RELAY, false);
}

void bms_contactor_init(bms_contactor_ctx_t *ctx)
{
    ctx->state = CONTACTOR_OPEN;
    ctx->state_timer_ms = 0U;
    ctx->bus_voltage_mv = 0U;
    ctx->close_requested = false;
    ctx->open_requested = false;
    all_contactors_off();
}

void bms_contactor_request_close(bms_contactor_ctx_t *ctx,
                                  uint32_t bus_voltage_mv)
{
    if (ctx->state == CONTACTOR_OPEN) {
        ctx->close_requested = true;
        ctx->bus_voltage_mv = bus_voltage_mv;
    }
}

void bms_contactor_request_open(bms_contactor_ctx_t *ctx)
{
    if (ctx->state == CONTACTOR_CLOSED || ctx->state == CONTACTOR_PRE_CHARGE ||
        ctx->state == CONTACTOR_CLOSING) {
        ctx->open_requested = true;
    }
}

void bms_contactor_run(bms_contactor_ctx_t *ctx,
                        bms_pack_data_t *pack,
                        uint32_t dt_ms)
{
    ctx->state_timer_ms += dt_ms;

    switch (ctx->state) {

    case CONTACTOR_OPEN:
        if (ctx->close_requested) {
            ctx->close_requested = false;
            ctx->state = CONTACTOR_PRE_CHARGE;
            ctx->state_timer_ms = 0U;

            /* Close pre-charge relay + main negative */
            hal_gpio_write(GPIO_CONTACTOR_NEG, true);
            hal_gpio_write(GPIO_PRECHARGE_RELAY, true);

            BMS_LOG("Contactor: OPEN -> PRE_CHARGE");
        }
        break;

    case CONTACTOR_PRE_CHARGE:
        if (ctx->open_requested) {
            ctx->open_requested = false;
            ctx->state = CONTACTOR_OPENING;
            ctx->state_timer_ms = 0U;
            all_contactors_off();
            BMS_LOG("Contactor: PRE_CHARGE -> OPENING (abort)");
            break;
        }

        /* Check if pack voltage has reached target (95% of bus) */
        {
            uint32_t target_mv = (ctx->bus_voltage_mv * BMS_PRECHARGE_VOLT_PCT) / 100U;
            if (pack->pack_voltage_mv >= target_mv) {
                /* Pre-charge complete, transition to CLOSING */
                ctx->state = CONTACTOR_CLOSING;
                ctx->state_timer_ms = 0U;

                /* Close main positive, open pre-charge */
                hal_gpio_write(GPIO_CONTACTOR_POS, true);
                hal_gpio_write(GPIO_PRECHARGE_RELAY, false);

                BMS_LOG("Contactor: PRE_CHARGE -> CLOSING");
            }
            /* Timeout check */
            else if (ctx->state_timer_ms >= BMS_PRECHARGE_TIMEOUT_MS) {
                BMS_LOG("Contactor: PRE_CHARGE timeout");
                ctx->state = CONTACTOR_OPEN;
                ctx->state_timer_ms = 0U;
                all_contactors_off();
            }
        }
        break;

    case CONTACTOR_CLOSING:
        if (ctx->open_requested) {
            ctx->open_requested = false;
            ctx->state = CONTACTOR_OPENING;
            ctx->state_timer_ms = 0U;
            all_contactors_off();
            break;
        }

        /* Verify contactor feedback — both contactors should read closed */
        {
            bool pos_fb = hal_gpio_read(GPIO_CONTACTOR_FB_POS);
            bool neg_fb = hal_gpio_read(GPIO_CONTACTOR_FB_NEG);

            if (pos_fb && neg_fb) {
                ctx->state = CONTACTOR_CLOSED;
                ctx->state_timer_ms = 0U;
                pack->contactor_state = CONTACTOR_CLOSED;
                BMS_LOG("Contactor: CLOSING -> CLOSED (verified)");
            }
            else if (ctx->state_timer_ms >= BMS_CONTACTOR_CLOSE_MS) {
                /* Feedback not confirmed in time — abort */
                BMS_LOG("Contactor: CLOSING feedback timeout");
                ctx->state = CONTACTOR_OPEN;
                ctx->state_timer_ms = 0U;
                all_contactors_off();
            }
        }
        break;

    case CONTACTOR_CLOSED:
        if (ctx->open_requested) {
            ctx->open_requested = false;
            ctx->state = CONTACTOR_OPENING;
            ctx->state_timer_ms = 0U;
            all_contactors_off();
            BMS_LOG("Contactor: CLOSED -> OPENING");
        }
        break;

    case CONTACTOR_OPENING:
        /* Welding detection: after opening, current should drop to near-zero */
        {
            int32_t abs_current = pack->pack_current_ma;
            if (abs_current < 0) { abs_current = -abs_current; }

            if (abs_current < 1000) { /* < 1A = confirmed open */
                ctx->state = CONTACTOR_OPEN;
                ctx->state_timer_ms = 0U;
                pack->contactor_state = CONTACTOR_OPEN;
                BMS_LOG("Contactor: OPENING -> OPEN (confirmed)");
            }
            else if (ctx->state_timer_ms >= BMS_WELD_DETECT_MS) {
                /* Current still flowing — contactor welded! */
                ctx->state = CONTACTOR_WELDED;
                pack->contactor_state = CONTACTOR_WELDED;
                pack->faults.contactor_weld = 1U;
                pack->fault_latched = true;
                BMS_LOG("Contactor: WELDED FAULT detected! I=%d mA",
                        (int)pack->pack_current_ma);
            }
        }
        break;

    case CONTACTOR_WELDED:
        /* Permanent fault state — requires manual intervention */
        break;
    }
}

bms_contactor_state_t bms_contactor_get_state(const bms_contactor_ctx_t *ctx)
{
    return ctx->state;
}

bool bms_contactor_is_faulted(const bms_contactor_ctx_t *ctx)
{
    return (ctx->state == CONTACTOR_WELDED);
}
