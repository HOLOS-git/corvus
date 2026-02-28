/**
 * test_nvm.c — NVM fault logging tests
 */

#include "bms_nvm.h"
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

extern void mock_nvm_reset(void);

static bms_nvm_ctx_t s_ctx;

static void setup(void)
{
    mock_nvm_reset();
    bms_nvm_init(&s_ctx);
}

/* ── Test: init gives empty context ────────────────────────────────── */
static void test_init(void)
{
    setup();
    TEST_ASSERT_EQ(s_ctx.fault_count, 0U);
    TEST_ASSERT_EQ(s_ctx.fault_head, 0U);
}

/* ── Test: log and retrieve one fault ──────────────────────────────── */
static void test_log_one(void)
{
    bms_nvm_fault_event_t ev;
    setup();

    bms_nvm_log_fault(&s_ctx, 1000U, 1U, 42U, 4300U);
    TEST_ASSERT_EQ(s_ctx.fault_count, 1U);

    TEST_ASSERT(bms_nvm_get_fault(&s_ctx, 0U, &ev));
    TEST_ASSERT_EQ(ev.timestamp_ms, 1000U);
    TEST_ASSERT_EQ(ev.fault_type, 1U);
    TEST_ASSERT_EQ(ev.cell_index, 42U);
    TEST_ASSERT_EQ(ev.value, 4300U);
}

/* ── Test: ring buffer wraps at 64 ─────────────────────────────────── */
static void test_ring_buffer_wrap(void)
{
    uint8_t i;
    bms_nvm_fault_event_t ev;
    setup();

    /* Log 70 events */
    for (i = 0U; i < 70U; i++) {
        bms_nvm_log_fault(&s_ctx, (uint32_t)i * 100U, i, i, (uint16_t)(i * 10U));
    }
    TEST_ASSERT_EQ(s_ctx.fault_count, 64U);

    /* Most recent should be event 69 */
    TEST_ASSERT(bms_nvm_get_fault(&s_ctx, 0U, &ev));
    TEST_ASSERT_EQ(ev.timestamp_ms, 6900U);
    TEST_ASSERT_EQ(ev.fault_type, 69U);

    /* Second most recent = event 68 */
    TEST_ASSERT(bms_nvm_get_fault(&s_ctx, 1U, &ev));
    TEST_ASSERT_EQ(ev.timestamp_ms, 6800U);
}

/* ── Test: get fault out of range returns false ────────────────────── */
static void test_get_out_of_range(void)
{
    bms_nvm_fault_event_t ev;
    setup();
    bms_nvm_log_fault(&s_ctx, 100U, 1U, 0U, 0U);
    TEST_ASSERT(!bms_nvm_get_fault(&s_ctx, 1U, &ev));
}

/* ── Test: persistent save/load ────────────────────────────────────── */
static void test_persistent(void)
{
    bms_nvm_ctx_t ctx2;
    setup();

    s_ctx.persistent.soc_hundredths = 7500U;
    s_ctx.persistent.runtime_hours = 42U;
    s_ctx.persistent.total_charge_mah = 1000000U;
    s_ctx.persistent.total_discharge_mah = 900000U;
    bms_nvm_save_persistent(&s_ctx);

    /* Load into fresh context */
    memset(&ctx2, 0, sizeof(ctx2));
    bms_nvm_load_persistent(&ctx2);
    TEST_ASSERT_EQ(ctx2.persistent.soc_hundredths, 7500U);
    TEST_ASSERT_EQ(ctx2.persistent.runtime_hours, 42U);
    TEST_ASSERT_EQ(ctx2.persistent.total_charge_mah, 1000000U);
    TEST_ASSERT_EQ(ctx2.persistent.total_discharge_mah, 900000U);
}

/* ── Test: fault log persists across reinit ────────────────────────── */
static void test_fault_persistence(void)
{
    bms_nvm_ctx_t ctx2;
    bms_nvm_fault_event_t ev;
    setup();

    bms_nvm_log_fault(&s_ctx, 5000U, 2U, 10U, 2900U);

    /* Reinit from NVM */
    memset(&ctx2, 0, sizeof(ctx2));
    bms_nvm_init(&ctx2);
    TEST_ASSERT_EQ(ctx2.fault_count, 1U);
    TEST_ASSERT(bms_nvm_get_fault(&ctx2, 0U, &ev));
    TEST_ASSERT_EQ(ev.timestamp_ms, 5000U);
    TEST_ASSERT_EQ(ev.fault_type, 2U);
}

void test_nvm_suite(void)
{
    test_init();
    test_log_one();
    test_ring_buffer_wrap();
    test_get_out_of_range();
    test_persistent();
    test_fault_persistence();
}
