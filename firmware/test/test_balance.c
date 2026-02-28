/**
 * test_balance.c — Cell balancing tests
 */

#include "bms_balance.h"
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

extern uint16_t mock_get_balance_mask(uint8_t module_id);

static bms_pack_data_t s_pack;
static bms_balance_state_t s_bal;

static void setup(void)
{
    uint8_t mod;
    uint8_t cell;
    memset(&s_pack, 0, sizeof(s_pack));
    bms_balance_init(&s_bal);

    s_pack.mode = BMS_MODE_READY;
    s_pack.pack_current_ma = 0;

    /* All cells at 3675 mV */
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        for (cell = 0U; cell < BMS_SE_PER_MODULE; cell++) {
            s_pack.modules[mod].cell_mv[cell] = 3675U;
            s_pack.cell_mv[(uint16_t)mod * BMS_SE_PER_MODULE + cell] = 3675U;
        }
    }
    s_pack.max_cell_mv = 3675U;
    s_pack.min_cell_mv = 3675U;
}

/* ── Test: no balancing when cells balanced ────────────────────────── */
static void test_no_balance_when_balanced(void)
{
    setup();
    bms_balance_run(&s_bal, &s_pack);
    TEST_ASSERT(!s_bal.active);
    TEST_ASSERT_EQ(s_bal.cell_mask[0], 0U);
}

/* ── Test: balancing activates with imbalance ──────────────────────── */
static void test_balance_activates(void)
{
    setup();
    /* Create imbalance: one cell high */
    s_pack.modules[0].cell_mv[0] = 3700U;
    s_pack.max_cell_mv = 3700U;
    s_pack.min_cell_mv = 3675U;
    /* Imbalance = 25 mV > 20 mV threshold */

    bms_balance_run(&s_bal, &s_pack);
    TEST_ASSERT(s_bal.active);
    /* Cell 0 of module 0 should be balancing (3700 > 3675 + 10 = 3685) */
    TEST_ASSERT(s_bal.cell_mask[0] & 0x01U);
}

/* ── Test: no balancing in FAULT mode ──────────────────────────────── */
static void test_no_balance_in_fault(void)
{
    setup();
    s_pack.modules[0].cell_mv[0] = 3700U;
    s_pack.max_cell_mv = 3700U;
    s_pack.mode = BMS_MODE_FAULT;

    bms_balance_run(&s_bal, &s_pack);
    TEST_ASSERT(!s_bal.active);
}

/* ── Test: no balancing during high-current charge ─────────────────── */
static void test_no_balance_high_current(void)
{
    setup();
    s_pack.modules[0].cell_mv[0] = 3700U;
    s_pack.max_cell_mv = 3700U;
    s_pack.pack_current_ma = 100000; /* ~0.78C, above 0.2C threshold */

    bms_balance_run(&s_bal, &s_pack);
    TEST_ASSERT(!s_bal.active);
}

/* ── Test: balancing ok at low current ─────────────────────────────── */
static void test_balance_low_current(void)
{
    setup();
    s_pack.modules[0].cell_mv[0] = 3700U;
    s_pack.max_cell_mv = 3700U;
    s_pack.pack_current_ma = 10000; /* ~0.08C, below 0.2C */

    bms_balance_run(&s_bal, &s_pack);
    TEST_ASSERT(s_bal.active);
}

/* ── Test: balancing stops when cells converge ─────────────────────── */
static void test_balance_stops(void)
{
    setup();
    /* Start with imbalance */
    s_pack.modules[0].cell_mv[0] = 3700U;
    s_pack.max_cell_mv = 3700U;
    bms_balance_run(&s_bal, &s_pack);
    TEST_ASSERT(s_bal.active);

    /* Cells converge */
    s_pack.modules[0].cell_mv[0] = 3680U;
    s_pack.max_cell_mv = 3680U;
    /* Imbalance now 5 mV < 20 mV */
    bms_balance_run(&s_bal, &s_pack);
    TEST_ASSERT(!s_bal.active);
}

/* ── Test: balance in CONNECTED mode too ───────────────────────────── */
static void test_balance_connected(void)
{
    setup();
    s_pack.mode = BMS_MODE_CONNECTED;
    s_pack.modules[0].cell_mv[0] = 3700U;
    s_pack.max_cell_mv = 3700U;

    bms_balance_run(&s_bal, &s_pack);
    TEST_ASSERT(s_bal.active);
}

void test_balance_suite(void)
{
    test_no_balance_when_balanced();
    test_balance_activates();
    test_no_balance_in_fault();
    test_no_balance_high_current();
    test_balance_low_current();
    test_balance_stops();
    test_balance_connected();
}
