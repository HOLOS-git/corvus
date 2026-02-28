/**
 * test_contactor.c — Contactor state machine tests
 */

#include "bms_contactor.h"
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
extern bool mock_get_gpio_output(bms_gpio_pin_t pin);

static bms_contactor_ctx_t s_ctx;
static bms_pack_data_t s_pack;

static void setup(void)
{
    mock_hal_reset();
    memset(&s_pack, 0, sizeof(s_pack));
    bms_contactor_init(&s_ctx);
    s_pack.pack_voltage_mv = 0U;
    s_pack.pack_current_ma = 0;
}

/* ── Init state is OPEN ────────────────────────────────────────────── */
static void test_init_state(void)
{
    setup();
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_OPEN);
    TEST_ASSERT(!bms_contactor_is_faulted(&s_ctx));
}

/* ── Close request transitions to PRE_CHARGE ───────────────────────── */
static void test_close_request_precharge(void)
{
    setup();
    bms_contactor_request_close(&s_ctx, 50000U);
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_PRE_CHARGE);
    /* Pre-charge relay and neg contactor should be on */
    TEST_ASSERT(mock_get_gpio_output(GPIO_CONTACTOR_NEG));
    TEST_ASSERT(mock_get_gpio_output(GPIO_PRECHARGE_RELAY));
    TEST_ASSERT(!mock_get_gpio_output(GPIO_CONTACTOR_POS));
}

/* ── Pre-charge complete → CLOSING when voltage reached ────────────── */
static void test_precharge_complete(void)
{
    setup();
    bms_contactor_request_close(&s_ctx, 50000U);
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_PRE_CHARGE);

    /* Simulate voltage reaching 95% of 50000 = 47500 */
    s_pack.pack_voltage_mv = 48000U;
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_CLOSING);
    /* Main positive should now be on, precharge off */
    TEST_ASSERT(mock_get_gpio_output(GPIO_CONTACTOR_POS));
    TEST_ASSERT(!mock_get_gpio_output(GPIO_PRECHARGE_RELAY));
}

/* ── CLOSING → CLOSED when feedback confirmed ──────────────────────── */
static void test_closing_to_closed(void)
{
    setup();
    bms_contactor_request_close(&s_ctx, 50000U);
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    s_pack.pack_voltage_mv = 48000U;
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_CLOSING);

    /* Set feedback inputs */
    mock_set_gpio_input(GPIO_CONTACTOR_FB_POS, true);
    mock_set_gpio_input(GPIO_CONTACTOR_FB_NEG, true);
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_CLOSED);
}

/* ── Pre-charge timeout returns to OPEN ────────────────────────────── */
static void test_precharge_timeout(void)
{
    uint32_t t;
    setup();
    bms_contactor_request_close(&s_ctx, 50000U);
    bms_contactor_run(&s_ctx, &s_pack, 10U);

    /* Voltage never reaches target */
    for (t = 0U; t < BMS_PRECHARGE_TIMEOUT_MS + 100U; t += 10U) {
        bms_contactor_run(&s_ctx, &s_pack, 10U);
    }
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_OPEN);
}

/* ── Open from CLOSED → OPENING → OPEN ────────────────────────────── */
static void test_open_from_closed(void)
{
    setup();
    /* Get to CLOSED state */
    bms_contactor_request_close(&s_ctx, 50000U);
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    s_pack.pack_voltage_mv = 48000U;
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    mock_set_gpio_input(GPIO_CONTACTOR_FB_POS, true);
    mock_set_gpio_input(GPIO_CONTACTOR_FB_NEG, true);
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_CLOSED);

    /* Request open */
    bms_contactor_request_open(&s_ctx);
    s_pack.pack_current_ma = 0; /* no current = confirmed open */
    bms_contactor_run(&s_ctx, &s_pack, 10U); /* → OPENING */
    bms_contactor_run(&s_ctx, &s_pack, 10U); /* → OPEN (current < 1A) */
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_OPEN);
}

/* ── Weld detection: current persists after open ───────────────────── */
static void test_weld_detection(void)
{
    uint32_t t;
    setup();
    /* Get to CLOSED */
    bms_contactor_request_close(&s_ctx, 50000U);
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    s_pack.pack_voltage_mv = 48000U;
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    mock_set_gpio_input(GPIO_CONTACTOR_FB_POS, true);
    mock_set_gpio_input(GPIO_CONTACTOR_FB_NEG, true);
    bms_contactor_run(&s_ctx, &s_pack, 10U);

    /* Request open but current persists */
    bms_contactor_request_open(&s_ctx);
    s_pack.pack_current_ma = 50000; /* 50A still flowing */
    for (t = 0U; t < BMS_WELD_DETECT_MS + 100U; t += 10U) {
        bms_contactor_run(&s_ctx, &s_pack, 10U);
    }
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_WELDED);
    TEST_ASSERT(bms_contactor_is_faulted(&s_ctx));
    TEST_ASSERT(s_pack.fault_latched);
}

/* ── Abort pre-charge via open request ─────────────────────────────── */
static void test_abort_precharge(void)
{
    setup();
    bms_contactor_request_close(&s_ctx, 50000U);
    bms_contactor_run(&s_ctx, &s_pack, 10U);
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_PRE_CHARGE);

    bms_contactor_request_open(&s_ctx);
    s_pack.pack_current_ma = 0;
    bms_contactor_run(&s_ctx, &s_pack, 10U); /* → OPENING */
    bms_contactor_run(&s_ctx, &s_pack, 10U); /* → OPEN (current < 1A) */
    TEST_ASSERT_EQ(bms_contactor_get_state(&s_ctx), CONTACTOR_OPEN);
}

void test_contactor_suite(void)
{
    test_init_state();
    test_close_request_precharge();
    test_precharge_complete();
    test_closing_to_closed();
    test_precharge_timeout();
    test_open_from_closed();
    test_weld_detection();
    test_abort_precharge();
}
