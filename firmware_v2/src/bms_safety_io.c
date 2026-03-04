/**
 * @file bms_safety_io.c
 * @brief Gas/Ventilation/Fire/IMD safety I/O interfaces
 *
 * Street Smart Edition. NEW — not in original firmware.
 * Reviewer findings addressed:
 *   P1-03: Gas detection (Catherine CRITICAL, Henrik SHOWSTOPPER)
 *     "DNVGL-RU-SHIP Pt.6 Ch.2 Sec.1 §1.4 mandatory requirement"
 *     "H₂/CO detection provides 30-120s earlier warning than temp sensors"
 *   P1-04: Ventilation monitoring (Henrik SHOWSTOPPER)
 *     "Battery room ventilation failure → hydrogen accumulation → explosion"
 *   P1-05: Fire detection/suppression (Henrik MAJOR, Priya CRITICAL)
 *     "Post-thermal-fault: manual-only reset, not 60s timer"
 *   P1-06: Insulation monitoring (Dave CRITICAL, Mikael CRITICAL, Henrik)
 *     "1000Vdc system with zero ground fault detection"
 */

#include "bms_safety_io.h"
#include "bms_hal.h"
#include "bms_nvm.h"
#include "bms_config.h"
#include <string.h>

/* P1-06: NVM context for IMD resistance trend logging */
static bms_nvm_ctx_t *s_sio_nvm_ctx;

void bms_safety_io_set_nvm(bms_nvm_ctx_t *nvm)
{
    s_sio_nvm_ctx = nvm;
}

void bms_safety_io_init(bms_safety_io_state_t *sio)
{
    memset(sio, 0, sizeof(*sio));
    sio->gas_level = SAFETY_IO_NORMAL;
    sio->vent_level = SAFETY_IO_NORMAL;
    sio->fire_level = SAFETY_IO_NORMAL;
    sio->imd_level = SAFETY_IO_NORMAL;
    sio->vent_running = true;  /* assume running until proven otherwise */
}

void bms_safety_io_run(bms_safety_io_state_t *sio,
                       bms_pack_data_t *pack)
{
    /* ── P1-03: Gas Detection ──────────────────────────────────────── */
    {
        bool gas_low  = hal_gpio_read(GPIO_GAS_ALARM_LOW);
        bool gas_high = hal_gpio_read(GPIO_GAS_ALARM_HIGH);

        if (gas_high) {
            /* High alarm → emergency shutdown within 2s */
            sio->gas_level = SAFETY_IO_SHUTDOWN;
            pack->faults.gas_alarm_high = 1U;
            pack->fault_latched = true;
            /* Command emergency ventilation */
            hal_gpio_write(GPIO_VENT_CMD, true);
            BMS_LOG("P1-03: GAS HIGH ALARM — emergency shutdown");
        } else if (gas_low) {
            /* Low alarm → warning + increase ventilation */
            sio->gas_level = SAFETY_IO_WARNING;
            pack->faults.gas_alarm_low = 1U;
            pack->has_warning = true;
            hal_gpio_write(GPIO_VENT_CMD, true);
        } else {
            sio->gas_level = SAFETY_IO_NORMAL;
            pack->faults.gas_alarm_low = 0U;
        }
    }

    /* ── P1-04: Ventilation Monitoring ─────────────────────────────── */
    {
        bool vent_ok = hal_gpio_read(GPIO_VENT_STATUS);
        sio->vent_running = vent_ok;

        if (!vent_ok) {
            sio->vent_level = SAFETY_IO_ALARM;
            pack->faults.vent_failure = 1U;
            pack->has_warning = true;
            sio->vent_restore_timer_ms = 0U;
            BMS_LOG("P1-04: Ventilation failure detected");
        } else if (sio->vent_level == SAFETY_IO_ALARM) {
            /* Vent restored — wait configurable delay */
            sio->vent_restore_timer_ms += BMS_SAFETY_IO_PERIOD_MS;
            if (sio->vent_restore_timer_ms >= BMS_VENT_RESTORE_DELAY_MS) {
                sio->vent_level = SAFETY_IO_NORMAL;
                pack->faults.vent_failure = 0U;
                BMS_LOG("P1-04: Ventilation restored after delay");
            }
        }
    }

    /* ── P1-05: Fire Detection/Suppression ─────────────────────────── */
    {
        bool fire_detect   = hal_gpio_read(GPIO_FIRE_DETECT);
        bool fire_suppress = hal_gpio_read(GPIO_FIRE_SUPPRESS_IN);

        if (fire_detect) {
            sio->fire_level = SAFETY_IO_SHUTDOWN;
            pack->faults.fire_detected = 1U;
            pack->fault_latched = true;
            /* Output to fire panel */
            hal_gpio_write(GPIO_FIRE_RELAY_OUT, true);
            BMS_LOG("P1-05: FIRE DETECTED — disconnect + interlock");
        }

        if (fire_suppress) {
            sio->fire_suppression_active = true;
            pack->faults.fire_suppression = 1U;
            pack->fault_latched = true;
            /* Post-incident interlock: manual-only reset */
            BMS_LOG("P1-05: Suppression active — manual reset required");
        }

        /* Output dT/dt alarm to fire panel as distinct warning */
        if (pack->faults.dtdt_alarm) {
            hal_gpio_write(GPIO_FIRE_RELAY_OUT, true);
        }
    }

    /* ── P1-06: Insulation Monitoring (IMD) ────────────────────────── */
    {
        bool imd_alarm = hal_gpio_read(GPIO_IMD_ALARM);

        /* Read insulation resistance from IMD analog output (IEC 61557-8) */
        uint16_t imd_adc = hal_adc_read(ADC_IMD_RESISTANCE);
        sio->imd_resistance_kohm = (uint32_t)imd_adc * BMS_IMD_ADC_SCALE_KOHM_PER_BIT;

        /* Configurable alarm threshold per IEC 61557-8 */
        if (imd_alarm || sio->imd_resistance_kohm < BMS_IMD_ALARM_THRESHOLD_KOHM) {
            sio->imd_level = SAFETY_IO_SHUTDOWN;
            pack->faults.imd_alarm = 1U;
            pack->fault_latched = true;
            BMS_LOG("P1-06: IMD ALARM — R_iso=%u kOhm (threshold=%u), pack disconnect",
                    (unsigned)sio->imd_resistance_kohm, BMS_IMD_ALARM_THRESHOLD_KOHM);
        } else {
            sio->imd_level = SAFETY_IO_NORMAL;
        }

        /* P3-02: Smart IMD resistance trend logging (Catherine — flash endurance)
         *
         * V2 logged every 60s → 100K-endurance flash exhausted in ~70 days.
         * New strategy:
         *   - Below warning threshold (<200kΩ): rapid logging (60s) for safety
         *   - Above warning: log only on significant change (>10% delta)
         *   - Fallback: hourly logging for trend data if no significant change
         */
        sio->imd_log_timer_ms += BMS_SAFETY_IO_PERIOD_MS;
        if (s_sio_nvm_ctx != NULL) {
            bool should_log = false;
            uint32_t r = sio->imd_resistance_kohm;

            if (r < BMS_IMD_WARNING_THRESHOLD_KOHM) {
                /* Below warning: rapid logging for safety trending */
                if (sio->imd_log_timer_ms >= BMS_IMD_LOG_INTERVAL_RAPID_MS) {
                    should_log = true;
                }
            } else {
                /* Above warning: log on significant change (>10% delta) */
                uint32_t last = sio->imd_last_logged_kohm;
                uint32_t delta = (r > last) ? (r - last) : (last - r);
                if (last > 0U && delta * 10U > last) {
                    /* >10% change from last logged value */
                    should_log = true;
                } else if (sio->imd_log_timer_ms >= BMS_IMD_LOG_INTERVAL_SLOW_MS) {
                    /* Hourly fallback for trend data */
                    should_log = true;
                }
            }

            if (should_log) {
                sio->imd_log_timer_ms = 0U;
                sio->imd_last_logged_kohm = r;
                bms_nvm_log_fault(s_sio_nvm_ctx, pack->uptime_ms,
                                  NVM_FAULT_IMD_TREND, 0xFFU,
                                  (uint16_t)r);
            }
        }
    }
}

bool bms_safety_io_inhibit_close(const bms_safety_io_state_t *sio)
{
    /* P1-04: Vent failure inhibits contactor closure */
    if (sio->vent_level == SAFETY_IO_ALARM) { return true; }
    /* Any shutdown-level condition */
    if (sio->gas_level == SAFETY_IO_SHUTDOWN) { return true; }
    if (sio->fire_level == SAFETY_IO_SHUTDOWN) { return true; }
    if (sio->imd_level == SAFETY_IO_SHUTDOWN) { return true; }
    return false;
}

bool bms_safety_io_emergency_shutdown(const bms_safety_io_state_t *sio)
{
    return (sio->gas_level == SAFETY_IO_SHUTDOWN ||
            sio->fire_level == SAFETY_IO_SHUTDOWN ||
            sio->imd_level == SAFETY_IO_SHUTDOWN);
}

void bms_safety_io_encode_can(const bms_safety_io_state_t *sio,
                               bms_can_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->id = CAN_ID_SAFETY_IO;
    frame->dlc = 8U;
    frame->data[0] = (uint8_t)sio->gas_level;
    frame->data[1] = (uint8_t)sio->vent_level;
    frame->data[2] = (uint8_t)sio->fire_level;
    frame->data[3] = (uint8_t)sio->imd_level;
    frame->data[4] = sio->fire_suppression_active ? 1U : 0U;
    frame->data[5] = sio->vent_running ? 1U : 0U;
    /* [6:7] reserved */
}
