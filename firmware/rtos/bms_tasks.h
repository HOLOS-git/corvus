/**
 * bms_tasks.h — FreeRTOS task declarations
 *
 * Task          Priority   Period   Stack
 * monitor       HIGH       10ms     1024 words
 * protection    HIGH       10ms     512 words
 * can_tx        MEDIUM     100ms    512 words
 * can_rx        MEDIUM     event    512 words
 * contactor     MEDIUM     50ms     256 words
 * state         LOW        100ms    512 words
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#ifndef BMS_TASKS_H
#define BMS_TASKS_H

#include "bms_types.h"
#include "bms_protection.h"
#include "bms_contactor.h"

/* ── Task priorities (FreeRTOS convention: higher number = higher priority) */
#define BMS_TASK_PRIO_HIGH     3U
#define BMS_TASK_PRIO_MEDIUM   2U
#define BMS_TASK_PRIO_LOW      1U

/* ── Stack sizes (words) ───────────────────────────────────────────── */
#define BMS_STACK_MONITOR      1024U
#define BMS_STACK_PROTECTION   512U
#define BMS_STACK_CAN_TX       512U
#define BMS_STACK_CAN_RX       512U
#define BMS_STACK_CONTACTOR    256U
#define BMS_STACK_STATE        512U

/**
 * Create and start all BMS tasks.
 * Must be called after hal_init() and subsystem init.
 */
void bms_tasks_create(bms_pack_data_t *pack,
                       bms_protection_state_t *prot,
                       bms_contactor_ctx_t *contactor);

#endif /* BMS_TASKS_H */
