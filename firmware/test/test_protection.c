/**
 * test_protection.c — Fault detection tests (per-cell, leaky timers)
 */

#include "bms_protection.h"
#include "bms_monitor.h"
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
extern void mock_set_all_cell_voltages(uint16_t mv);
extern void mock_set_cell_voltage(uint8_t mod, uint8_t cell, uint16_t mv);
extern void mock_set_all_temperatures(int16_t deci_c);
extern void mock_set_temperature(uint8_t mod, uint8_t sens, int16_t deci_c);

static bms_pack_data_t s_pack;
static bms_protection_state_t s_prot;

static void setup_nominal(void)
{
    uint16_t i;
    mock_hal_reset();
    memset(&s_pack, 0, sizeof(s_pack));
    bms_protection_init(&s_prot);

    /* Set all cells to nominal 3675 mV */
    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        s_pack.cell_mv[i] = 3675U;
    }
    /* Set all temps to 25.0°C = 250 deci-C */
    {
        uint8_t mod, sens;
        for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
            for (sens = 0U; sens < BMS_TEMPS_PER_MODULE; sens++) {
                s_pack.modules[mod].temp_deci_c[sens] = 250;
            }
        }
    }
    s_pack.max_temp_deci_c = 250;
    s_pack.min_temp_deci_c = 250;
    s_pack.pack_current_ma = 0;
}

/* ── Test: no fault under normal conditions ────────────────────────── */
static void test_no_fault_nominal(void)
{
    setup_nominal();
    bms_protection_run(&s_prot, &s_pack, 10U);
    TEST_ASSERT(!s_pack.fault_latched);
    TEST_ASSERT_EQ(s_pack.faults.cell_ov, 0U);
    TEST_ASSERT_EQ(s_pack.faults.cell_uv, 0U);
}

/* ── Test: single cell OV after delay ──────────────────────────────── */
static void test_single_cell_ov(void)
{
    uint32_t t;
    setup_nominal();

    /* Set cell 42 to OV fault threshold */
    s_pack.cell_mv[42] = BMS_SE_OV_FAULT_MV;

    /* Run for less than fault delay — should not trip */
    for (t = 0U; t < 4900U; t += 10U) {
        bms_protection_run(&s_prot, &s_pack, 10U);
    }
    TEST_ASSERT(!s_pack.fault_latched);

    /* Run past the 5s threshold */
    for (t = 0U; t < 200U; t += 10U) {
        bms_protection_run(&s_prot, &s_pack, 10U);
    }
    TEST_ASSERT(s_pack.fault_latched);
    TEST_ASSERT_EQ(s_pack.faults.cell_ov, 1U);
}

/* ── Test: single cell UV after delay ──────────────────────────────── */
static void test_single_cell_uv(void)
{
    uint32_t t;
    setup_nominal();

    s_pack.cell_mv[100] = BMS_SE_UV_FAULT_MV;

    for (t = 0U; t < 5100U; t += 10U) {
        bms_protection_run(&s_prot, &s_pack, 10U);
    }
    TEST_ASSERT(s_pack.fault_latched);
    TEST_ASSERT_EQ(s_pack.faults.cell_uv, 1U);
}

/* ── Test: leaky timer decay — transient OV should NOT trip ────────── */
static void test_leaky_timer_decay(void)
{
    uint32_t t;
    setup_nominal();

    /* OV for 2 seconds */
    s_pack.cell_mv[10] = BMS_SE_OV_FAULT_MV;
    for (t = 0U; t < 2000U; t += 10U) {
        bms_protection_run(&s_prot, &s_pack, 10U);
    }
    TEST_ASSERT(!s_pack.fault_latched);

    /* Clear for 6 seconds (timer should decay to 0 at dt/2 rate) */
    s_pack.cell_mv[10] = 3675U;
    for (t = 0U; t < 6000U; t += 10U) {
        bms_protection_run(&s_prot, &s_pack, 10U);
    }
    TEST_ASSERT(!s_pack.fault_latched);

    /* Timer should be at or near 0 */
    TEST_ASSERT(s_prot.ov_timer_ms[10] < 100U);
}

/* ── Test: OT fault ────────────────────────────────────────────────── */
static void test_overtemperature_fault(void)
{
    uint32_t t;
    setup_nominal();

    /* Set one sensor to OT fault threshold */
    s_pack.modules[5].temp_deci_c[1] = BMS_SE_OT_FAULT_DECI_C;
    s_pack.max_temp_deci_c = BMS_SE_OT_FAULT_DECI_C;

    for (t = 0U; t < 5100U; t += 10U) {
        bms_protection_run(&s_prot, &s_pack, 10U);
    }
    TEST_ASSERT(s_pack.fault_latched);
    TEST_ASSERT_EQ(s_pack.faults.cell_ot, 1U);
}

/* ── Test: HW safety OV (faster, independent) ─────────────────────── */
static void test_hw_safety_ov(void)
{
    uint32_t t;
    setup_nominal();

    /* HW OV: 4300mV, 1s delay */
    s_pack.cell_mv[0] = BMS_HW_OV_MV;

    for (t = 0U; t < 1100U; t += 10U) {
        bms_protection_run(&s_prot, &s_pack, 10U);
    }
    TEST_ASSERT(s_pack.fault_latched);
    TEST_ASSERT_EQ(s_pack.faults.hw_ov, 1U);
}

/* ── Test: OC charge fault ─────────────────────────────────────────── */
static void test_overcurrent_charge(void)
{
    uint32_t t;
    setup_nominal();

    s_pack.pack_current_ma = BMS_MAX_CHARGE_MA + 1000; /* exceed limit */

    for (t = 0U; t < 5100U; t += 10U) {
        bms_protection_run(&s_prot, &s_pack, 10U);
    }
    TEST_ASSERT(s_pack.fault_latched);
    TEST_ASSERT_EQ(s_pack.faults.oc_charge, 1U);
}

/* ── Test: fault reset after safe-state hold ───────────────────────── */
static void test_fault_reset(void)
{
    uint32_t t;
    setup_nominal();

    /* Trip a fault */
    s_pack.cell_mv[0] = BMS_SE_OV_FAULT_MV;
    for (t = 0U; t < 5100U; t += 10U) {
        bms_protection_run(&s_prot, &s_pack, 10U);
    }
    TEST_ASSERT(s_pack.fault_latched);

    /* Clear condition */
    s_pack.cell_mv[0] = 3675U;

    /* Not enough safe-state time yet */
    for (t = 0U; t < 30000U; t += 10U) {
        bms_protection_run(&s_prot, &s_pack, 10U);
    }
    TEST_ASSERT(!bms_protection_can_reset(&s_prot, &s_pack));

    /* Accumulate full 60s */
    for (t = 0U; t < 31000U; t += 10U) {
        bms_protection_run(&s_prot, &s_pack, 10U);
    }
    TEST_ASSERT(bms_protection_can_reset(&s_prot, &s_pack));

    /* Reset */
    bms_protection_reset(&s_prot, &s_pack);
    TEST_ASSERT(!s_pack.fault_latched);
}

/* ── Test: warning flag on OV warning threshold ────────────────────── */
static void test_warning_ov(void)
{
    setup_nominal();
    s_pack.cell_mv[200] = BMS_SE_OV_WARN_MV;

    bms_protection_run(&s_prot, &s_pack, 10U);
    TEST_ASSERT(s_pack.has_warning);
    TEST_ASSERT(!s_pack.fault_latched);
}

void test_protection_suite(void)
{
    test_no_fault_nominal();
    test_single_cell_ov();
    test_single_cell_uv();
    test_leaky_timer_decay();
    test_overtemperature_fault();
    test_hw_safety_ov();
    test_overcurrent_charge();
    test_fault_reset();
    test_warning_ov();
}
