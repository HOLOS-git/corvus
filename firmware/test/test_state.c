/**
 * test_state.c — Pack state machine tests
 */

#include "bms_state.h"
#include "bms_contactor.h"
#include "bms_protection.h"
#include "bms_hal.h"
#include "bms_config.h"
#include <stdio.h>
#include <string.h>

extern int g_tests_run, g_tests_passed, g_tests_failed;
#define TEST_ASSERT(expr) do { \
    g_tests_run++; \
    if (expr) { g_tests_passed++; } \
    else { g_tests_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); } \
} while (0)
#define TEST_ASSERT_EQ(a, b) do { \
    g_tests_run++; \
    if ((a) == (b)) { g_tests_passed++; } \
    else { g_tests_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: %s == %s (%ld != %ld)\n", \
                __FILE__, __LINE__, #a, #b, (long)(a), (long)(b)); } \
} while (0)

extern void mock_hal_reset(void);
extern void mock_set_gpio_input(bms_gpio_pin_t pin, bool state);

static bms_pack_data_t s_pack;
static bms_contactor_ctx_t s_contactor;
static bms_protection_state_t s_prot;

static void setup(void)
{
    uint8_t mod;
    mock_hal_reset();
    memset(&s_pack, 0, sizeof(s_pack));
    bms_contactor_init(&s_contactor);
    bms_protection_init(&s_prot);
    bms_state_init(&s_pack);
    s_pack.uptime_ms = 0U;
    s_pack.last_ems_msg_ms = 0U;
    /* Mark all modules as comm_ok for self-test */
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        s_pack.modules[mod].comm_ok = true;
    }
}

/* ── Init starts NOT_READY ─────────────────────────────────────────── */
static void test_init_not_ready(void)
{
    memset(&s_pack, 0, sizeof(s_pack));
    bms_state_init(&s_pack);
    TEST_ASSERT_EQ((int)s_pack.mode, (int)BMS_MODE_NOT_READY);
}

/* ── NOT_READY → READY when all modules comm_ok ────────────────────── */
static void test_not_ready_to_ready(void)
{
    setup();
    bms_state_run(&s_pack, &s_contactor, &s_prot, NULL, 100U);
    TEST_ASSERT_EQ((int)s_pack.mode, (int)BMS_MODE_READY);
}

/* ── NOT_READY stays if a module has no comm ───────────────────────── */
static void test_not_ready_stays(void)
{
    setup();
    /* Re-init to NOT_READY and break one module */
    bms_state_init(&s_pack);
    s_pack.modules[5].comm_ok = false;
    bms_state_run(&s_pack, &s_contactor, &s_prot, NULL, 100U);
    TEST_ASSERT_EQ((int)s_pack.mode, (int)BMS_MODE_NOT_READY);
}

/* ── READY → CONNECTING on EMS connect command ─────────────────────── */
static void test_ready_to_connecting(void)
{
    bms_ems_command_t cmd;
    setup();
    bms_state_run(&s_pack, &s_contactor, &s_prot, NULL, 100U); /* → READY */

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = EMS_CMD_CONNECT_CHG;
    bms_state_run(&s_pack, &s_contactor, &s_prot, &cmd, 100U);
    TEST_ASSERT_EQ((int)s_pack.mode, (int)BMS_MODE_CONNECTING);
}

/* ── READY → POWER_SAVE on EMS power save command ──────────────────── */
static void test_ready_to_power_save(void)
{
    bms_ems_command_t cmd;
    setup();
    bms_state_run(&s_pack, &s_contactor, &s_prot, NULL, 100U); /* → READY */

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = EMS_CMD_POWER_SAVE;
    bms_state_run(&s_pack, &s_contactor, &s_prot, &cmd, 100U);
    TEST_ASSERT_EQ((int)s_pack.mode, (int)BMS_MODE_POWER_SAVE);
}

/* ── POWER_SAVE → READY on wake command ────────────────────────────── */
static void test_power_save_wake(void)
{
    bms_ems_command_t cmd;
    setup();
    bms_state_run(&s_pack, &s_contactor, &s_prot, NULL, 100U); /* → READY */

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = EMS_CMD_POWER_SAVE;
    bms_state_run(&s_pack, &s_contactor, &s_prot, &cmd, 100U); /* → POWER_SAVE */

    cmd.type = EMS_CMD_CONNECT_CHG;
    bms_state_run(&s_pack, &s_contactor, &s_prot, &cmd, 100U);
    TEST_ASSERT_EQ((int)s_pack.mode, (int)BMS_MODE_READY);
}

/* ── Fault latched → FAULT from any state ──────────────────────────── */
static void test_fault_from_ready(void)
{
    setup();
    bms_state_run(&s_pack, &s_contactor, &s_prot, NULL, 100U); /* → READY */

    s_pack.fault_latched = true;
    bms_state_run(&s_pack, &s_contactor, &s_prot, NULL, 100U);
    TEST_ASSERT_EQ((int)s_pack.mode, (int)BMS_MODE_FAULT);
    TEST_ASSERT_EQ(s_pack.charge_limit_ma, 0);
    TEST_ASSERT_EQ(s_pack.discharge_limit_ma, 0);
}

/* ── Mode name lookup ──────────────────────────────────────────────── */
static void test_mode_names(void)
{
    TEST_ASSERT(bms_state_mode_name(BMS_MODE_READY) != NULL);
    TEST_ASSERT(bms_state_mode_name(BMS_MODE_FAULT) != NULL);
    TEST_ASSERT(bms_state_mode_name(BMS_MODE_NOT_READY) != NULL);
}

/* ── CONNECTING → READY when contactor stays open (precharge fail) ── */
static void test_connecting_precharge_fail(void)
{
    uint32_t t;
    bms_ems_command_t cmd;
    setup();
    bms_state_run(&s_pack, &s_contactor, &s_prot, NULL, 100U); /* → READY */

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = EMS_CMD_CONNECT_CHG;
    cmd.timestamp_ms = s_pack.uptime_ms;
    s_pack.last_ems_msg_ms = s_pack.uptime_ms;
    bms_state_run(&s_pack, &s_contactor, &s_prot, &cmd, 100U); /* → CONNECTING */
    TEST_ASSERT_EQ((int)s_pack.mode, (int)BMS_MODE_CONNECTING);

    /* Run contactor past precharge timeout with no voltage rise */
    /* Keep sending EMS commands to prevent watchdog */
    cmd.type = EMS_CMD_CONNECT_CHG;
    for (t = 0U; t < BMS_PRECHARGE_TIMEOUT_MS + 200U; t += 50U) {
        s_pack.uptime_ms += 50U;
        cmd.timestamp_ms = s_pack.uptime_ms;
        s_pack.last_ems_msg_ms = s_pack.uptime_ms;
        bms_contactor_run(&s_contactor, &s_pack, 50U);
        bms_state_run(&s_pack, &s_contactor, &s_prot, &cmd, 50U);
    }
    /* Contactor should have timed out back to OPEN, state → READY */
    TEST_ASSERT_EQ((int)s_pack.mode, (int)BMS_MODE_READY);
}

void test_state_suite(void)
{
    test_init_not_ready();
    test_not_ready_to_ready();
    test_not_ready_stays();
    test_ready_to_connecting();
    test_ready_to_power_save();
    test_power_save_wake();
    test_fault_from_ready();
    test_mode_names();
    test_connecting_precharge_fail();
}
