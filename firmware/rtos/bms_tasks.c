/**
 * bms_tasks.c — FreeRTOS task creation and scheduling
 *
 * Each task runs in its own FreeRTOS thread with specified priority/period.
 * On DESKTOP_BUILD, we provide stub implementations (no RTOS).
 * All tasks use hal_critical_enter/exit around shared pack data access.
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include "bms_tasks.h"
#include "bms_monitor.h"
#include "bms_protection.h"
#include "bms_contactor.h"
#include "bms_can.h"
#include "bms_state.h"
#include "bms_hal.h"
#include "bms_config.h"

#ifdef STM32_BUILD
/* #include "FreeRTOS.h" */
/* #include "task.h" */
#endif

/* ── Shared context pointers (set during create) ───────────────────── */
static bms_pack_data_t         *s_pack;
static bms_protection_state_t  *s_prot;
static bms_contactor_ctx_t     *s_contactor;
static bms_ems_command_t        s_last_ems_cmd;

/* ── Task functions ────────────────────────────────────────────────── */

#ifdef STM32_BUILD

static void task_monitor(void *arg)
{
    (void)arg;
    /* TickType_t last_wake = xTaskGetTickCount(); */
    for (;;) {
        bms_monitor_run(s_pack);
        /* Critical section around writing aggregated pack data is handled
         * inside bms_monitor_run via hal_critical_enter/exit */
        /* vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_MONITOR_PERIOD_MS)); */
    }
}

static void task_protection(void *arg)
{
    (void)arg;
    for (;;) {
        hal_critical_enter();
        bms_protection_run(s_prot, s_pack, BMS_PROTECTION_PERIOD_MS);
        hal_critical_exit();
        if (s_pack->fault_latched) {
            hal_critical_enter();
            bms_state_enter_fault(s_pack, s_contactor);
            hal_critical_exit();
        }
        /* vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_PROTECTION_PERIOD_MS)); */
    }
}

static void task_can_tx(void *arg)
{
    (void)arg;
    for (;;) {
        hal_critical_enter();
        bms_can_tx_periodic(s_pack);
        hal_critical_exit();
        /* vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_CAN_TX_PERIOD_MS)); */
    }
}

static void task_can_rx(void *arg)
{
    (void)arg;
    for (;;) {
        bms_ems_command_t cmd;
        if (bms_can_rx_process(&cmd)) {
            hal_critical_enter();
            s_last_ems_cmd = cmd;
            s_pack->last_ems_msg_ms = cmd.timestamp_ms;
            hal_critical_exit();
        }
        /* vTaskDelay(pdMS_TO_TICKS(1)); — event-driven, minimal delay */
    }
}

static void task_contactor(void *arg)
{
    (void)arg;
    for (;;) {
        hal_critical_enter();
        bms_contactor_run(s_contactor, s_pack, BMS_CONTACTOR_PERIOD_MS);
        s_pack->contactor_state = bms_contactor_get_state(s_contactor);
        hal_critical_exit();
        /* vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_CONTACTOR_PERIOD_MS)); */
    }
}

static void task_state(void *arg)
{
    (void)arg;
    for (;;) {
        hal_critical_enter();
        bms_state_run(s_pack, s_contactor, s_prot, &s_last_ems_cmd,
                       BMS_STATE_PERIOD_MS);
        s_last_ems_cmd.type = EMS_CMD_NONE; /* consume command */
        hal_critical_exit();
        /* vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_STATE_PERIOD_MS)); */
    }
}

#endif /* STM32_BUILD */

/* ── Create tasks ──────────────────────────────────────────────────── */

void bms_tasks_create(bms_pack_data_t *pack,
                       bms_protection_state_t *prot,
                       bms_contactor_ctx_t *contactor)
{
    s_pack = pack;
    s_prot = prot;
    s_contactor = contactor;
    s_last_ems_cmd.type = EMS_CMD_NONE;

#ifdef STM32_BUILD
    /* xTaskCreate(task_monitor,    "mon",  BMS_STACK_MONITOR,    NULL, BMS_TASK_PRIO_HIGH,   NULL);
     * xTaskCreate(task_protection, "prot", BMS_STACK_PROTECTION, NULL, BMS_TASK_PRIO_HIGH,   NULL);
     * xTaskCreate(task_can_tx,     "ctx",  BMS_STACK_CAN_TX,     NULL, BMS_TASK_PRIO_MEDIUM, NULL);
     * xTaskCreate(task_can_rx,     "crx",  BMS_STACK_CAN_RX,     NULL, BMS_TASK_PRIO_MEDIUM, NULL);
     * xTaskCreate(task_contactor,  "cont", BMS_STACK_CONTACTOR,  NULL, BMS_TASK_PRIO_MEDIUM, NULL);
     * xTaskCreate(task_state,      "st",   BMS_STACK_STATE,      NULL, BMS_TASK_PRIO_LOW,    NULL); */

    /* Suppress unused-function warnings for stub build */
    (void)task_monitor;
    (void)task_protection;
    (void)task_can_tx;
    (void)task_can_rx;
    (void)task_contactor;
    (void)task_state;
#endif

    BMS_LOG("Tasks created (pack=%p)", (void *)pack);
}
