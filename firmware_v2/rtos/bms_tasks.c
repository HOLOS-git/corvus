/**
 * @file bms_tasks.c
 * @brief FreeRTOS task definitions for BMS firmware
 *
 * Street Smart Edition.
 * Alternative to the bare-metal main loop in main.c.
 * Use this when building with FreeRTOS; otherwise the cooperative
 * scheduler in main.c provides equivalent timing.
 *
 * Task priorities (higher = more important):
 *   5: Protection + IWDG feed   (10ms, safety-critical)
 *   4: Monitor                  (10ms, data acquisition)
 *   3: Contactor control        (50ms)
 *   2: Safety I/O + State + CAN (100ms)
 *   1: Thermal dT/dt            (1000ms)
 */

#ifdef USE_FREERTOS

#include "FreeRTOS.h"
#include "task.h"

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

/* ── Shared state (defined in main.c via extern) ───────────────────── */

extern bms_pack_data_t          g_pack;
extern bms_protection_state_t   g_prot;
extern bms_thermal_state_t      g_thermal;
extern bms_safety_io_state_t    g_safety_io;
extern bms_contactor_ctx_t      g_contactor;
extern bms_nvm_ctx_t            g_nvm;
extern bms_ems_command_t        g_ems_cmd;

/* ── Stack sizes ───────────────────────────────────────────────────── */

#define BMS_TASK_STACK_MONITOR      512U
#define BMS_TASK_STACK_PROTECTION   512U
#define BMS_TASK_STACK_CONTACTOR    256U
#define BMS_TASK_STACK_STATE        384U
#define BMS_TASK_STACK_CAN          256U
#define BMS_TASK_STACK_THERMAL      256U
#define BMS_TASK_STACK_SAFETY_IO    256U

/* ── Task handles ──────────────────────────────────────────────────── */

static TaskHandle_t h_monitor;
static TaskHandle_t h_protection;
static TaskHandle_t h_contactor;
static TaskHandle_t h_state;
static TaskHandle_t h_can;
static TaskHandle_t h_thermal;
static TaskHandle_t h_safety_io;

/* ═══════════════════════════════════════════════════════════════════════
 * Task: Protection + IWDG feed (10ms, priority 5)
 * HIGHEST priority — if this task starves, watchdog resets the MCU.
 * ═══════════════════════════════════════════════════════════════════════ */
static void task_protection(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        /* P3-05: Critical section around shared fault flag access (Dave) */
        BMS_ENTER_CRITICAL();
        bms_protection_run(&g_prot, &g_pack, BMS_PROTECTION_PERIOD_MS);
        BMS_EXIT_CRITICAL();
        hal_iwdg_feed();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_PROTECTION_PERIOD_MS));
    }
}

/* ── Task: Monitor (10ms, priority 4) ──────────────────────────────── */
static void task_monitor(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        /* P3-05: Critical section around shared pack data writes (Dave) */
        BMS_ENTER_CRITICAL();
        bms_monitor_run(&g_pack);
        BMS_EXIT_CRITICAL();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_MONITOR_PERIOD_MS));
    }
}

/* ── Task: Contactor (50ms, priority 3) ────────────────────────────── */
static void task_contactor(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        bms_contactor_run(&g_contactor, &g_pack, BMS_CONTACTOR_PERIOD_MS);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_CONTACTOR_PERIOD_MS));
    }
}

/* ── Task: Safety I/O (100ms, priority 2) ──────────────────────────── */
static void task_safety_io(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        /* P3-05: Critical section around shared fault flag access (Dave) */
        BMS_ENTER_CRITICAL();
        bms_safety_io_run(&g_safety_io, &g_pack);
        BMS_EXIT_CRITICAL();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_SAFETY_IO_PERIOD_MS));
    }
}

/* ── Task: State machine + CAN RX (100ms, priority 2) ─────────────── */
static void task_state(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        /* P3-05: Critical section around shared state + fault flag access (Dave) */
        BMS_ENTER_CRITICAL();
        (void)bms_can_rx_process(&g_ems_cmd);
        bms_state_run(&g_pack, &g_contactor, &g_prot,
                      &g_safety_io, &g_ems_cmd, BMS_STATE_PERIOD_MS);
        BMS_EXIT_CRITICAL();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_STATE_PERIOD_MS));
    }
}

/* ── Task: CAN TX (100ms, priority 2) ──────────────────────────────── */
static void task_can_tx(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        /* P3-05: Critical section around shared pack data reads (Dave) */
        BMS_ENTER_CRITICAL();
        bms_can_tx_periodic(&g_pack);
        BMS_EXIT_CRITICAL();

        /* Safety I/O CAN frame */
        {
            bms_can_frame_t sio_frame;
            BMS_ENTER_CRITICAL();
            bms_safety_io_encode_can(&g_safety_io, &sio_frame);
            BMS_EXIT_CRITICAL();
            (void)hal_can_transmit(&sio_frame);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_CAN_TX_PERIOD_MS));
    }
}

/* ── Task: Thermal dT/dt (1000ms, priority 1) ─────────────────────── */
static void task_thermal(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        bms_thermal_run(&g_thermal, &g_pack, BMS_THERMAL_PERIOD_MS);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BMS_THERMAL_PERIOD_MS));
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * Create all RTOS tasks. Called from main() after bms_init_all().
 * ═══════════════════════════════════════════════════════════════════════ */
void bms_tasks_create(void)
{
    xTaskCreate(task_protection, "prot",  BMS_TASK_STACK_PROTECTION, NULL, 5, &h_protection);
    xTaskCreate(task_monitor,    "mon",   BMS_TASK_STACK_MONITOR,    NULL, 4, &h_monitor);
    xTaskCreate(task_contactor,  "cont",  BMS_TASK_STACK_CONTACTOR,  NULL, 3, &h_contactor);
    xTaskCreate(task_safety_io,  "sio",   BMS_TASK_STACK_SAFETY_IO,  NULL, 2, &h_safety_io);
    xTaskCreate(task_state,      "state", BMS_TASK_STACK_STATE,      NULL, 2, &h_state);
    xTaskCreate(task_can_tx,     "can",   BMS_TASK_STACK_CAN,        NULL, 2, &h_can);
    xTaskCreate(task_thermal,    "therm", BMS_TASK_STACK_THERMAL,    NULL, 1, &h_thermal);
}

#endif /* USE_FREERTOS */
