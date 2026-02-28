/**
 * firmware_demo.c — Desktop demo harness for Corvus Orca ESS firmware
 *
 * Simulates a single pack through multiple phases:
 *   init → ready → connecting → connected (charging) → OV warning →
 *   OT fault → reset → reconnect → discharge → shutdown
 *
 * Externally simulates battery physics (simple OCV + IR model) and feeds
 * voltages/temps into the mock HAL, then lets firmware protection/state react.
 *
 * Outputs CSV to stdout for plotting.
 *
 * Build: gcc -DDESKTOP_BUILD -Wall -Wextra -Werror -pedantic -std=c99
 *        -Iinc -o firmware_demo demo/firmware_demo.c src/ hal/hal_mock.c -lm
 */

#include "bms_types.h"
#include "bms_config.h"
#include "bms_monitor.h"
#include "bms_protection.h"
#include "bms_state.h"
#include "bms_contactor.h"
#include "bms_soc.h"
#include "bms_current_limit.h"
#include "bms_can.h"
#include "bms_hal.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── External mock HAL API ─────────────────────────────────────────── */
extern void mock_hal_reset(void);
extern void mock_set_all_cell_voltages(uint16_t mv);
extern void mock_set_cell_voltage(uint8_t module_id, uint8_t cell_idx, uint16_t mv);
extern void mock_set_all_temperatures(int16_t deci_c);
extern void mock_set_temperature(uint8_t module_id, uint8_t sensor_idx, int16_t deci_c);
extern void mock_set_gpio_input(bms_gpio_pin_t pin, bool state);
extern void mock_set_tick(uint32_t ms);
extern void mock_set_adc(bms_adc_channel_t ch, uint16_t val);

/* ── Simple battery model ──────────────────────────────────────────── */

/* OCV table (same 24 points as firmware) */
static const double ocv_soc[] = {
    0.0, 2.0, 5.0, 8.0, 10.0, 15.0, 20.0, 25.0,
    30.0, 35.0, 40.0, 45.0, 50.0, 55.0, 60.0, 65.0,
    70.0, 75.0, 80.0, 85.0, 90.0, 95.0, 98.0, 100.0
};
static const double ocv_mv[] = {
    3000, 3280, 3420, 3480, 3510, 3555, 3590, 3610,
    3625, 3638, 3650, 3662, 3675, 3690, 3710, 3735,
    3765, 3800, 3845, 3900, 3960, 4030, 4100, 4190
};
#define OCV_PTS 24

static double ocv_from_soc(double soc_pct)
{
    int i;
    if (soc_pct <= ocv_soc[0]) return ocv_mv[0];
    if (soc_pct >= ocv_soc[OCV_PTS-1]) return ocv_mv[OCV_PTS-1];
    for (i = 1; i < OCV_PTS; i++) {
        if (soc_pct <= ocv_soc[i]) {
            double frac = (soc_pct - ocv_soc[i-1]) / (ocv_soc[i] - ocv_soc[i-1]);
            return ocv_mv[i-1] + frac * (ocv_mv[i] - ocv_mv[i-1]);
        }
    }
    return ocv_mv[OCV_PTS-1];
}

/* Internal resistance in milliohms */
#define IR_MOHM 0.5

/* Pack capacity in Ah */
#define CAPACITY_AH 128.0

/* ── Scenario phases ───────────────────────────────────────────────── */
typedef enum {
    PHASE_INIT,           /* 0-2s: init, self-test */
    PHASE_CONNECT_CHG,    /* 2-4s: EMS connect for charging */
    PHASE_CHARGE,         /* 4-300s: charge at ~200A */
    PHASE_OV_WARNING,     /* 300-350s: push voltage to OV warning */
    PHASE_OT_RAMP,        /* 350-500s: ramp temperature to OT fault */
    PHASE_FAULT_HOLD,     /* 500-570s: in fault, waiting for safe state */
    PHASE_COOL_DOWN,      /* 570-630s: cool down, reset */
    PHASE_RECONNECT,      /* 630-640s: reconnect */
    PHASE_DISCHARGE,      /* 640-900s: discharge */
    PHASE_SHUTDOWN,       /* 900-950s: disconnect and finish */
    PHASE_DONE
} demo_phase_t;

/* ── Main ──────────────────────────────────────────────────────────── */
int main(void)
{
    bms_pack_data_t pack;
    bms_protection_state_t prot;
    bms_contactor_ctx_t contactor;
    bms_ems_command_t cmd;

    /* Simulation state */
    double soc_pct = 55.0;         /* Start at 55% SoC */
    double temp_deci_c = 350.0;    /* 35.0°C */
    double current_ma = 0.0;       /* applied current */
    uint32_t time_ms = 0;
    uint32_t dt_ms = 100;          /* 100ms steps */
    demo_phase_t phase = PHASE_INIT;
    bool fault_reset_sent = false;

    /* Initialize everything */
    memset(&pack, 0, sizeof(pack));
    memset(&cmd, 0, sizeof(cmd));

    hal_init();
    mock_set_all_cell_voltages(3675);
    mock_set_all_temperatures(350);

    /* Set contactor feedback pins high to simulate working contactors */
    mock_set_gpio_input(GPIO_CONTACTOR_FB_POS, true);
    mock_set_gpio_input(GPIO_CONTACTOR_FB_NEG, true);

    /* Set bus voltage ADC (pack voltage ~ 308 * 3.675V = 1131.9V → in 10mV units) */
    mock_set_adc(ADC_BUS_VOLTAGE, 2048);

    bms_monitor_init(&pack);
    bms_protection_init(&prot);
    bms_contactor_init(&contactor);
    bms_state_init(&pack);
    bms_soc_init((uint16_t)(soc_pct * 100.0));
    bms_can_init();

    /* Make all modules comm_ok for self-test pass */
    {
        uint8_t m;
        for (m = 0; m < BMS_NUM_MODULES; m++) {
            pack.modules[m].comm_ok = true;
        }
    }

    /* CSV header */
    printf("time_s,soc_pct,cell_mv,temperature_deci_c,current_ma,"
           "charge_limit_ma,discharge_limit_ma,mode,contactor_state,"
           "warnings,faults\n");

    /* ── Main simulation loop ──────────────────────────────────────── */
    while (phase != PHASE_DONE && time_ms <= 1000000) {
        /* Clear command each step */
        cmd.type = EMS_CMD_NONE;
        cmd.timestamp_ms = time_ms;

        /* ── Phase transitions and external physics ──────────────── */
        switch (phase) {
        case PHASE_INIT:
            current_ma = 0.0;
            if (time_ms >= 2000) {
                phase = PHASE_CONNECT_CHG;
            }
            break;

        case PHASE_CONNECT_CHG:
            cmd.type = EMS_CMD_CONNECT_CHG;
            cmd.timestamp_ms = time_ms;
            pack.last_ems_msg_ms = time_ms;
            phase = PHASE_CHARGE;
            break;

        case PHASE_CHARGE:
            /* Heartbeat to keep EMS watchdog alive */
            cmd.type = EMS_CMD_SET_LIMITS;
            cmd.charge_limit_ma = 384000;
            cmd.discharge_limit_ma = 640000;
            cmd.timestamp_ms = time_ms;
            pack.last_ems_msg_ms = time_ms;

            /* Charge at 200A when connected */
            if (pack.mode == BMS_MODE_CONNECTED) {
                current_ma = 200000.0; /* 200A charge */
            } else {
                current_ma = 0.0;
            }

            if (time_ms >= 300000) {
                phase = PHASE_OV_WARNING;
            }
            break;

        case PHASE_OV_WARNING:
            /* Keep charging but push SoC high to trigger OV warning */
            cmd.type = EMS_CMD_SET_LIMITS;
            cmd.charge_limit_ma = 384000;
            cmd.discharge_limit_ma = 640000;
            cmd.timestamp_ms = time_ms;
            pack.last_ems_msg_ms = time_ms;

            /* Override: force cell voltage up toward OV warning */
            soc_pct = 98.0 + (double)(time_ms - 300000) / 50000.0 * 2.5;
            if (soc_pct > 100.0) soc_pct = 100.0;
            current_ma = 100000.0; /* slow charge */

            if (time_ms >= 350000) {
                phase = PHASE_OT_RAMP;
            }
            break;

        case PHASE_OT_RAMP:
            /* Ramp temperature up toward OT fault */
            cmd.type = EMS_CMD_SET_LIMITS;
            cmd.charge_limit_ma = 384000;
            cmd.discharge_limit_ma = 640000;
            cmd.timestamp_ms = time_ms;
            pack.last_ems_msg_ms = time_ms;

            current_ma = 50000.0;
            temp_deci_c = 350.0 + (double)(time_ms - 350000) / 150000.0 * 350.0;
            /* Ramp from 35.0°C to 70.0°C over 150s */

            if (pack.mode == BMS_MODE_FAULT || time_ms >= 500000) {
                phase = PHASE_FAULT_HOLD;
                current_ma = 0.0;
            }
            break;

        case PHASE_FAULT_HOLD:
            current_ma = 0.0;
            /* Cool down temp */
            if (temp_deci_c > 300.0) {
                temp_deci_c -= 0.5;  /* slow cool per 100ms step */
            }
            /* After 60s+ in fault, try reset */
            if (time_ms >= 570000 && !fault_reset_sent) {
                phase = PHASE_COOL_DOWN;
            }
            break;

        case PHASE_COOL_DOWN:
            current_ma = 0.0;
            /* Cool down faster */
            if (temp_deci_c > 300.0) {
                temp_deci_c -= 1.0;
            }
            /* Try fault reset when temp safe */
            if (temp_deci_c <= 400.0 && !fault_reset_sent) {
                cmd.type = EMS_CMD_RESET_FAULTS;
                cmd.timestamp_ms = time_ms;
                fault_reset_sent = true;
            }
            if (pack.mode == BMS_MODE_READY || time_ms >= 640000) {
                phase = PHASE_RECONNECT;
                /* Reset SoC to reasonable level for discharge demo */
                soc_pct = 75.0;
            }
            break;

        case PHASE_RECONNECT:
            cmd.type = EMS_CMD_CONNECT_DCHG;
            cmd.timestamp_ms = time_ms;
            pack.last_ems_msg_ms = time_ms;
            if (pack.mode == BMS_MODE_CONNECTED) {
                phase = PHASE_DISCHARGE;
            }
            if (time_ms >= 660000) {
                phase = PHASE_DISCHARGE;
            }
            break;

        case PHASE_DISCHARGE:
            cmd.type = EMS_CMD_SET_LIMITS;
            cmd.charge_limit_ma = 384000;
            cmd.discharge_limit_ma = 640000;
            cmd.timestamp_ms = time_ms;
            pack.last_ems_msg_ms = time_ms;

            if (pack.mode == BMS_MODE_CONNECTED) {
                current_ma = -150000.0; /* 150A discharge */
            } else {
                current_ma = 0.0;
            }

            if (time_ms >= 900000) {
                phase = PHASE_SHUTDOWN;
            }
            break;

        case PHASE_SHUTDOWN:
            cmd.type = EMS_CMD_DISCONNECT;
            cmd.timestamp_ms = time_ms;
            pack.last_ems_msg_ms = time_ms;
            current_ma = 0.0;

            if (time_ms >= 950000) {
                phase = PHASE_DONE;
            }
            break;

        case PHASE_DONE:
            break;
        }

        /* ── Update battery physics ────────────────────────────────── */
        /* Coulomb counting for SoC */
        soc_pct += (current_ma / 1000.0) * ((double)dt_ms / 3600000.0) / CAPACITY_AH * 100.0;
        if (soc_pct < 0.0) soc_pct = 0.0;
        if (soc_pct > 100.0) soc_pct = 100.0;

        /* Cell voltage = OCV(SoC) + IR*I (charge positive) */
        double cell_v = ocv_from_soc(soc_pct) + IR_MOHM * (current_ma / 1000.0);
        uint16_t cell_mv = (uint16_t)(cell_v > 0 ? cell_v : 0);

        /* ── Inject into mock HAL ──────────────────────────────────── */
        mock_set_all_cell_voltages(cell_mv);
        mock_set_all_temperatures((int16_t)temp_deci_c);
        mock_set_tick(time_ms);

        /* Set pack current — zero it if contactors aren't closed */
        if (pack.contactor_state != CONTACTOR_CLOSED) {
            pack.pack_current_ma = 0;
        } else {
            pack.pack_current_ma = (int32_t)current_ma;
        }

        /* Pack voltage for pre-charge check */
        pack.pack_voltage_mv = (uint32_t)cell_mv * BMS_SE_PER_PACK;

        /* ── Run firmware subsystems ───────────────────────────────── */
        bms_monitor_read_modules(&pack);
        bms_monitor_aggregate(&pack);
        bms_protection_run(&prot, &pack, dt_ms);

        /* Current limits */
        {
            int32_t chg_lim, dchg_lim;
            bms_current_limit_compute(&pack, &chg_lim, &dchg_lim);
            pack.charge_limit_ma = chg_lim;
            pack.discharge_limit_ma = dchg_lim;
        }

        /* SoC update */
        bms_soc_update(&pack, dt_ms);

        /* Contactor state machine */
        bms_contactor_run(&contactor, &pack, dt_ms);
        pack.contactor_state = bms_contactor_get_state(&contactor);

        /* State machine */
        pack.uptime_ms = time_ms;
        bms_state_run(&pack, &contactor, &prot, &cmd, dt_ms);

        /* ── Compute fault/warning word for CSV ────────────────────── */
        uint32_t fault_word;
        memcpy(&fault_word, &pack.faults, sizeof(fault_word));

        /* ── Output CSV row ────────────────────────────────────────── */
        printf("%.1f,%.2f,%u,%.0f,%d,%d,%d,%d,%d,%d,%u\n",
               (double)time_ms / 1000.0,
               soc_pct,
               (unsigned)cell_mv,
               temp_deci_c,
               (int)(current_ma),
               (int)pack.charge_limit_ma,
               (int)pack.discharge_limit_ma,
               (int)pack.mode,
               (int)pack.contactor_state,
               pack.has_warning ? 1 : 0,
               (unsigned)fault_word);

        time_ms += dt_ms;
    }

    return 0;
}
