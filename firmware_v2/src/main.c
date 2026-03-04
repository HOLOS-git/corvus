/**
 * @file main.c
 * @brief BMS firmware entry point — init sequence + RTOS task creation
 *
 * Street Smart Edition.
 * Startup sequence:
 *   1. HAL init (clocks, GPIO, peripherals)
 *   2. IWDG reset detection + NVM logging
 *   3. NVM init + load persistent data
 *   4. AFE init (BQ76952 per module — includes HW protection config)
 *   5. Monitor init (zero pack data)
 *   6. Protection init
 *   7. Thermal init
 *   8. Safety I/O init
 *   9. Contactor init (opens all contactors as fail-safe)
 *  10. State machine init
 *  11. CAN init (filter setup)
 *  12. IWDG init (start watchdog LAST — after all init completes)
 *  13. Start RTOS tasks (or enter main loop)
 */

#include "bms_hal.h"
#include "bms_config.h"
#include "bms_types.h"
#include "bms_bq76952.h"
#include "bms_monitor.h"
#include "bms_protection.h"
#include "bms_thermal.h"
#include "bms_safety_io.h"
#include "bms_contactor.h"
#include "bms_state.h"
#include "bms_can.h"
#include "bms_nvm.h"
#include "bms_balance.h"
#include "bms_soc.h"
#include "bms_current_limit.h"

/* ── Global state ──────────────────────────────────────────────────── */

static bms_pack_data_t          g_pack;
static bms_protection_state_t   g_prot;
static bms_thermal_state_t      g_thermal;
static bms_safety_io_state_t    g_safety_io;
static bms_contactor_ctx_t      g_contactor;
static bms_nvm_ctx_t            g_nvm;
static bms_ems_command_t        g_ems_cmd;

/* ── Forward declarations for task functions ───────────────────────── */

void bms_task_monitor(void);
void bms_task_protection(void);
void bms_task_can(void);
void bms_task_contactor(void);
void bms_task_state(void);
void bms_task_thermal(void);
void bms_task_safety_io(void);

/* ── Init sequence ─────────────────────────────────────────────────── */

static int32_t bms_init_all(void)
{
    uint8_t mod;
    int32_t rc;

    /* 1. HAL init — clocks, GPIO, I2C, CAN, ADC peripherals */
    hal_init();

    /* 2. NVM init + check for IWDG reset */
    bms_nvm_init(&g_nvm);
    bms_nvm_load_persistent(&g_nvm);

    if (hal_iwdg_was_reset()) {
        bms_nvm_log_fault(&g_nvm, 0U, NVM_FAULT_IWDG, 0xFFU, 0U);
        BMS_LOG("IWDG reset detected — logged to NVM");
    }

    /* 3. Wire NVM into protection for fault logging */
    bms_protection_set_nvm(&g_nvm);

    /* 4. AFE init — BQ76952 per module (includes HW protection config) */
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        rc = bq76952_init(mod);
        if (rc != 0) {
            BMS_LOG("FATAL: BQ76952 module %u init failed (%d)", mod, (int)rc);
            return rc;
        }
    }

    /* 5. Monitor init (zeroes pack data, inits SoC + balance) */
    bms_monitor_init(&g_pack);

    /* 6. Protection init */
    bms_protection_init(&g_prot);

    /* 7. Thermal init */
    bms_thermal_init(&g_thermal);

    /* 8. Safety I/O init + wire NVM for IMD trend logging */
    bms_safety_io_set_nvm(&g_nvm);
    bms_safety_io_init(&g_safety_io);

    /* 9. Contactor init — opens all contactors (fail-safe default) */
    bms_contactor_init(&g_contactor);

    /* 10. State machine init */
    bms_state_init(&g_pack);

    /* 11. CAN init (hardware filter setup) */
    bms_can_init();

    /* 12. Start IWDG LAST — all init must complete before watchdog runs */
    hal_iwdg_init(BMS_IWDG_TIMEOUT_MS);

    BMS_LOG("BMS init complete — all subsystems ready");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Main Loop (bare-metal cooperative scheduler)
 *
 * If using FreeRTOS, replace this with vTaskStartScheduler() and
 * create tasks in bms_tasks.c. This loop provides the same timing
 * guarantees via tick-based scheduling.
 * ═══════════════════════════════════════════════════════════════════════ */

int main(void)
{
    int32_t rc;
    uint32_t now, last_monitor, last_protection, last_can;
    uint32_t last_contactor, last_state, last_thermal, last_safety_io;

    rc = bms_init_all();
    if (rc != 0) {
        /* Init failed — enter safe state with contactors open (already done
         * by contactor_init). Spin forever; IWDG will reset us. */
        while (1) { /* IWDG will fire */ }
    }

    now = hal_tick_ms();
    last_monitor    = now;
    last_protection = now;
    last_can        = now;
    last_contactor  = now;
    last_state      = now;
    last_thermal    = now;
    last_safety_io  = now;

    while (1) {
        now = hal_tick_ms();

        /* ── 10ms: Monitor (cell voltage + temp reads) ─────────── */
        if ((now - last_monitor) >= BMS_MONITOR_PERIOD_MS) {
            last_monitor = now;
            bms_monitor_run(&g_pack);
        }

        /* ── 10ms: Protection (OV/UV/OT/OC/sub-zero checks) ───── */
        if ((now - last_protection) >= BMS_PROTECTION_PERIOD_MS) {
            last_protection = now;
            bms_protection_run(&g_prot, &g_pack, BMS_PROTECTION_PERIOD_MS);

            /* P1-02: Feed IWDG from protection loop.
             * If protection hangs, watchdog fires → safe reset. */
            hal_iwdg_feed();
        }

        /* ── 100ms: Safety I/O (gas/vent/fire/IMD) ─────────────── */
        if ((now - last_safety_io) >= BMS_SAFETY_IO_PERIOD_MS) {
            last_safety_io = now;
            bms_safety_io_run(&g_safety_io, &g_pack);
        }

        /* ── 50ms: Contactor control ───────────────────────────── */
        if ((now - last_contactor) >= BMS_CONTACTOR_PERIOD_MS) {
            last_contactor = now;
            bms_contactor_run(&g_contactor, &g_pack, BMS_CONTACTOR_PERIOD_MS);
        }

        /* ── 100ms: State machine ──────────────────────────────── */
        if ((now - last_state) >= BMS_STATE_PERIOD_MS) {
            last_state = now;

            /* Process CAN RX before state machine */
            (void)bms_can_rx_process(&g_ems_cmd);

            bms_state_run(&g_pack, &g_contactor, &g_prot,
                          &g_safety_io, &g_ems_cmd, BMS_STATE_PERIOD_MS);
        }

        /* ── 100ms: CAN TX ─────────────────────────────────────── */
        if ((now - last_can) >= BMS_CAN_TX_PERIOD_MS) {
            last_can = now;
            bms_can_tx_periodic(&g_pack);

            /* Also send safety I/O status */
            {
                bms_can_frame_t sio_frame;
                bms_safety_io_encode_can(&g_safety_io, &sio_frame);
                (void)hal_can_transmit(&sio_frame);
            }
        }

        /* ── 1000ms: Thermal dT/dt ────────────────────────────── */
        if ((now - last_thermal) >= BMS_THERMAL_PERIOD_MS) {
            last_thermal = now;
            bms_thermal_run(&g_thermal, &g_pack, BMS_THERMAL_PERIOD_MS);
        }
    }

    return 0; /* unreachable */
}
