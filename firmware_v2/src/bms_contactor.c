/**
 * @file bms_contactor.c
 * @brief Contactor state machine — uses BUS voltage for pre-charge
 *
 * Street Smart Edition.
 * Reviewer findings addressed:
 *   P0-03: Pre-charge uses BUS voltage from ADC, not pack voltage (Dave CRITICAL)
 *     "30V differential across the contactor at closure. Contactor welds.
 *      Permanent 1000V connection you can't break." — Dave
 *   CC-10: ADC_BUS_VOLTAGE defined but never used — NOW USED
 *   Extended weld detection window from 200ms to 500ms (Catherine, Dave)
 *   Voltage match check: |pack - bus| < BMS_VOLTAGE_MATCH_MV before main close
 */

#include "bms_contactor.h"
#include "bms_hal.h"
#include "bms_config.h"

static void all_contactors_off(void)
{
    hal_gpio_write(GPIO_CONTACTOR_POS, false);
    hal_gpio_write(GPIO_CONTACTOR_NEG, false);
    hal_gpio_write(GPIO_PRECHARGE_RELAY, false);
}

void bms_contactor_init(bms_contactor_ctx_t *ctx)
{
    /* Fail-safe: contactors open on init (P1-02: after IWDG reset) */
    ctx->state = CONTACTOR_OPEN;
    ctx->state_timer_ms = 0U;
    ctx->target_bus_voltage_mv = 0U;
    ctx->close_requested = false;
    ctx->open_requested = false;
    all_contactors_off();
}

void bms_contactor_request_close(bms_contactor_ctx_t *ctx,
                                  uint32_t bus_voltage_mv)
{
    if (ctx->state == CONTACTOR_OPEN) {
        ctx->close_requested = true;
        /* P0-03: Store ACTUAL bus voltage as pre-charge target */
        ctx->target_bus_voltage_mv = bus_voltage_mv;
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
            hal_gpio_write(GPIO_CONTACTOR_NEG, true);
            hal_gpio_write(GPIO_PRECHARGE_RELAY, true);
            BMS_LOG("Contactor: OPEN -> PRE_CHARGE (bus target=%u mV)",
                    (unsigned)ctx->target_bus_voltage_mv);
        }
        break;

    case CONTACTOR_PRE_CHARGE:
        if (ctx->open_requested) {
            ctx->open_requested = false;
            ctx->state = CONTACTOR_OPENING;
            ctx->state_timer_ms = 0U;
            all_contactors_off();
            break;
        }

        /* P0-03: Read ACTUAL bus voltage via ADC and check against target
         *
         * Safety rationale: Original code used pack->pack_voltage_mv as the
         * pre-charge target. This is WRONG — we need to match the bus voltage,
         * not our own voltage. If other packs are already on the bus at a
         * different voltage, closing into a mismatch welds contactors.
         */
        {
            /* P3-04: Read live bus voltage from ADC (Dave — ADC scaling fix)
             *
             * V2 had inconsistent scaling: contactor used ×1000/4 while monitor
             * used ×1000/4095. Both must use the same conversion. The ADC is
             * 12-bit (0–4095) with a resistor divider sized for the bus voltage
             * range. Use BMS_ADC_BUS_VOLTAGE_SCALE_NUM/DEN for a single
             * calibration point. */
            uint32_t current_bus_mv = (uint32_t)hal_adc_read(ADC_BUS_VOLTAGE);
            current_bus_mv = current_bus_mv * BMS_ADC_BUS_VOLTAGE_SCALE_NUM
                           / BMS_ADC_BUS_VOLTAGE_SCALE_DEN;

            uint32_t target_mv = (current_bus_mv * BMS_PRECHARGE_VOLT_PCT) / 100U;

            /* Check if pack voltage has reached target bus voltage */
            if (pack->pack_voltage_mv >= target_mv && target_mv > 0U) {
                /* P0-03: Voltage match check before closing main contactor */
                uint32_t diff;
                if (pack->pack_voltage_mv > current_bus_mv) {
                    diff = pack->pack_voltage_mv - current_bus_mv;
                } else {
                    diff = current_bus_mv - pack->pack_voltage_mv;
                }

                if (diff < BMS_VOLTAGE_MATCH_MV) {
                    ctx->state = CONTACTOR_CLOSING;
                    ctx->state_timer_ms = 0U;
                    hal_gpio_write(GPIO_CONTACTOR_POS, true);
                    hal_gpio_write(GPIO_PRECHARGE_RELAY, false);
                    BMS_LOG("P0-03: Voltage matched (diff=%u mV), closing main",
                            (unsigned)diff);
                } else {
                    BMS_LOG("P0-03: Voltage mismatch! pack=%u, bus=%u, diff=%u",
                            (unsigned)pack->pack_voltage_mv,
                            (unsigned)current_bus_mv, (unsigned)diff);
                    /* Continue pre-charging */
                }
            }

            if (ctx->state_timer_ms >= BMS_PRECHARGE_TIMEOUT_MS) {
                BMS_LOG("P0-03: Pre-charge timeout (bus mismatch)");
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
        {
            bool pos_fb = hal_gpio_read(GPIO_CONTACTOR_FB_POS);
            bool neg_fb = hal_gpio_read(GPIO_CONTACTOR_FB_NEG);
            if (pos_fb && neg_fb) {
                ctx->state = CONTACTOR_CLOSED;
                ctx->state_timer_ms = 0U;
                pack->contactor_state = CONTACTOR_CLOSED;
            } else if (ctx->state_timer_ms >= BMS_CONTACTOR_CLOSE_MS) {
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
        }
        break;

    case CONTACTOR_OPENING:
        /* Extended weld detection: 500ms window (was 200ms) per Catherine/Dave */
        {
            int32_t abs_current = pack->pack_current_ma;
            if (abs_current < 0) { abs_current = -abs_current; }

            if (abs_current < 1000) {
                ctx->state = CONTACTOR_OPEN;
                ctx->state_timer_ms = 0U;
                pack->contactor_state = CONTACTOR_OPEN;
            } else if (ctx->state_timer_ms >= BMS_WELD_DETECT_MS) {
                ctx->state = CONTACTOR_WELDED;
                pack->contactor_state = CONTACTOR_WELDED;
                pack->faults.contactor_weld = 1U;
                pack->fault_latched = true;
                BMS_LOG("CONTACTOR WELDED! I=%d mA", (int)pack->pack_current_ma);
            }
        }
        break;

    case CONTACTOR_WELDED:
        break; /* Permanent — requires manual intervention */
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
