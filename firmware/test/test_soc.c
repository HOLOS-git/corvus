/**
 * test_soc.c — SoC estimation tests
 */

#include "bms_soc.h"
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

static bms_pack_data_t s_pack;

static void setup(void)
{
    memset(&s_pack, 0, sizeof(s_pack));
    s_pack.soc_hundredths = 5000U;
    s_pack.avg_cell_mv = 3675U;
    s_pack.mode = BMS_MODE_READY;
    bms_soc_init(5000U);
}

/* ── Test: init value preserved ────────────────────────────────────── */
static void test_init(void)
{
    bms_soc_init(7500U);
    TEST_ASSERT_EQ(bms_soc_get(), 7500U);
}

/* ── Test: no current = no change ──────────────────────────────────── */
static void test_no_current(void)
{
    setup();
    s_pack.pack_current_ma = 0;
    bms_soc_update(&s_pack, 1000U);
    TEST_ASSERT_EQ(s_pack.soc_hundredths, 5000U);
}

/* ── Test: charging increases SoC ──────────────────────────────────── */
static void test_charging(void)
{
    setup();
    /* 128000 mA (1C) for 1 second: delta = 128000 * 1000 / (128000 * 360) = 2.78 */
    s_pack.pack_current_ma = 128000;
    bms_soc_update(&s_pack, 1000U);
    /* SoC should increase */
    TEST_ASSERT(s_pack.soc_hundredths > 5000U);
}

/* ── Test: discharging decreases SoC ───────────────────────────────── */
static void test_discharging(void)
{
    setup();
    s_pack.pack_current_ma = -128000; /* 1C discharge */
    bms_soc_update(&s_pack, 1000U);
    TEST_ASSERT(s_pack.soc_hundredths < 5000U);
}

/* ── Test: SoC clamps at 0 ────────────────────────────────────────── */
static void test_clamp_zero(void)
{
    setup();
    bms_soc_init(10U); /* start at 0.10% */
    s_pack.soc_hundredths = 10U;
    s_pack.pack_current_ma = -640000; /* max discharge */
    bms_soc_update(&s_pack, 10000U); /* 10 seconds */
    TEST_ASSERT_EQ(s_pack.soc_hundredths, 0U);
}

/* ── Test: SoC clamps at 10000 ─────────────────────────────────────── */
static void test_clamp_full(void)
{
    setup();
    bms_soc_init(9990U);
    s_pack.soc_hundredths = 9990U;
    s_pack.pack_current_ma = 384000; /* max charge */
    bms_soc_update(&s_pack, 10000U);
    TEST_ASSERT_EQ(s_pack.soc_hundredths, 10000U);
}

/* ── Test: OCV lookup — known points ───────────────────────────────── */
static void test_ocv_lookup(void)
{
    /* 3000 mV → 0% */
    TEST_ASSERT_EQ(bms_soc_from_ocv(3000U), 0U);
    /* 4190 mV → 100% */
    TEST_ASSERT_EQ(bms_soc_from_ocv(4190U), 10000U);
    /* 3675 mV → 5000 (50%) — exact match in table */
    TEST_ASSERT_EQ(bms_soc_from_ocv(3675U), 5000U);
}

/* ── Test: OCV lookup — below/above range ──────────────────────────── */
static void test_ocv_clamp(void)
{
    TEST_ASSERT_EQ(bms_soc_from_ocv(2500U), 0U);
    TEST_ASSERT_EQ(bms_soc_from_ocv(4500U), 10000U);
}

/* ── Test: OCV reset after 30s rest ────────────────────────────────── */
static void test_ocv_reset(void)
{
    uint32_t t;
    setup();
    bms_soc_init(5000U);
    s_pack.pack_current_ma = 0;
    s_pack.avg_cell_mv = 3900U; /* corresponds to ~85% SoC */
    s_pack.mode = BMS_MODE_READY;

    /* Run for 31 seconds to trigger OCV reset */
    for (t = 0U; t < 31000U; t += 100U) {
        s_pack.soc_hundredths = bms_soc_get();
        bms_soc_update(&s_pack, 100U);
    }

    /* SoC should now be near 85% (OCV table: 3900→8500) */
    TEST_ASSERT_EQ(s_pack.soc_hundredths, 8500U);
}

/* ── Test: OCV reset does NOT trigger during CONNECTED mode ────────── */
static void test_ocv_no_reset_connected(void)
{
    uint32_t t;
    setup();
    bms_soc_init(5000U);
    s_pack.pack_current_ma = 0;
    s_pack.avg_cell_mv = 3900U;
    s_pack.mode = BMS_MODE_CONNECTED; /* not READY */

    for (t = 0U; t < 35000U; t += 100U) {
        s_pack.soc_hundredths = bms_soc_get();
        bms_soc_update(&s_pack, 100U);
    }

    /* SoC should remain at 5000 (no OCV correction in CONNECTED) */
    TEST_ASSERT_EQ(s_pack.soc_hundredths, 5000U);
}

/* ── Test: coulomb counting overflow handling ──────────────────────── */
static void test_overflow_safety(void)
{
    setup();
    /* Large current × large dt — int64_t should handle it */
    s_pack.pack_current_ma = 640000;
    bms_soc_update(&s_pack, 60000U); /* 1 minute at 5C charge */
    /* Should not wrap — SoC should be larger but clamped at 10000 */
    TEST_ASSERT(s_pack.soc_hundredths <= 10000U);
    TEST_ASSERT(s_pack.soc_hundredths > 5000U);
}

void test_soc_suite(void)
{
    test_init();
    test_no_current();
    test_charging();
    test_discharging();
    test_clamp_zero();
    test_clamp_full();
    test_ocv_lookup();
    test_ocv_clamp();
    test_ocv_reset();
    test_ocv_no_reset_connected();
    test_overflow_safety();
}
