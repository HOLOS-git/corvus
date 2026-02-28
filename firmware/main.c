/**
 * main.c — BMS firmware entry point
 *
 * On STM32: initializes HAL, subsystems, creates FreeRTOS tasks, starts scheduler.
 * On Desktop: runs a single test cycle for verification.
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include "bms_types.h"
#include "bms_hal.h"
#include "bms_bq76952.h"
#include "bms_monitor.h"
#include "bms_protection.h"
#include "bms_contactor.h"
#include "bms_can.h"
#include "bms_state.h"
#include "bms_config.h"
#include "bms_tasks.h"

/* ── Static allocation — no malloc, no calloc ──────────────────────── */
static bms_pack_data_t         g_pack;
static bms_protection_state_t  g_prot;
static bms_contactor_ctx_t     g_contactor;

int main(void)
{
    uint8_t mod;

    /* Initialize hardware abstraction layer */
    hal_init();

    /* Initialize BQ76952 on each module */
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        if (bq76952_init(mod) != 0) {
            BMS_LOG("FATAL: BQ76952 init failed on module %u", mod);
        }
    }

    /* Initialize subsystems */
    bms_monitor_init(&g_pack);
    bms_protection_init(&g_prot);
    bms_contactor_init(&g_contactor);
    bms_can_init();
    bms_state_init(&g_pack);

    BMS_LOG("BMS firmware initialized — %u modules, %u cells",
            (unsigned)BMS_NUM_MODULES, (unsigned)BMS_SE_PER_PACK);

#ifdef STM32_BUILD
    /* Create FreeRTOS tasks and start scheduler */
    bms_tasks_create(&g_pack, &g_prot, &g_contactor);
    /* vTaskStartScheduler(); */
    /* Should never reach here */
    for (;;) {}
#endif

#ifdef DESKTOP_BUILD
    /* Desktop: run a few cycles to verify everything links */
    {
        uint8_t cycle;
        for (cycle = 0U; cycle < 10U; cycle++) {
            bms_monitor_run(&g_pack);
            bms_protection_run(&g_prot, &g_pack, BMS_MONITOR_PERIOD_MS);
            bms_contactor_run(&g_contactor, &g_pack, BMS_CONTACTOR_PERIOD_MS);
            bms_state_run(&g_pack, &g_contactor, &g_prot, NULL, BMS_STATE_PERIOD_MS);
            bms_can_tx_periodic(&g_pack);
        }
        BMS_LOG("Desktop run complete — mode=%s, V=%u mV, cells=%u",
                bms_state_mode_name(g_pack.mode),
                (unsigned)g_pack.pack_voltage_mv,
                (unsigned)BMS_SE_PER_PACK);
    }
#endif

    return 0;
}
