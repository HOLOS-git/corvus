/**
 * test_current_limit.c — Current derating curve tests
 */

#include "bms_current_limit.h"
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

static void setup_nominal(void)
{
    uint16_t i;
    uint8_t mod, sens;
    memset(&s_pack, 0, sizeof(s_pack));

    for (i = 0U; i < BMS_SE_PER_PACK; i++) {
        s_pack.cell_mv[i] = 3675U;
    }
    for (mod = 0U; mod < BMS_NUM_MODULES; mod++) {
        for (sens = 0U; sens < BMS_SE_PER_MODULE; sens++) {
            s_pack.modules[mod].cell_mv[sens] = 3675U;
        }
    }
    s_pack.max_cell_mv = 3675U;
    s_pack.min_cell_mv = 3675U;
    s_pack.avg_cell_mv = 3675U;
    s_pack.max_temp_deci_c = 250; /* 25°C */
    s_pack.soc_hundredths = 5000U; /* 50% */
}

/* ── Test: nominal conditions — full limits ────────────────────────── */
static void test_nominal_full_limits(void)
{
    int32_t chg, dchg;
    setup_nominal();

    bms_current_limit_compute(&s_pack, &chg, &dchg);

    /* At 25°C, 50% SoC, 3675mV:
       Temp charge: 3C = 384000mA
       Temp discharge: 5C = 640000mA
       SoC charge: 3C = 384000mA
       SoC discharge: 5C = 640000mA
       SEV charge: 3C (3675 < 4100)
       SEV discharge: 5C (3675 > 3550)
       Min of all = 384000 / 640000 */
    TEST_ASSERT_EQ(chg, 384000);
    TEST_ASSERT_EQ(dchg, 640000);
}

/* ── Test: cold temperature — charge limited ───────────────────────── */
static void test_cold_temp_charge_zero(void)
{
    int32_t chg, dchg;
    setup_nominal();
    s_pack.max_temp_deci_c = 0; /* 0°C */

    bms_current_limit_compute(&s_pack, &chg, &dchg);

    /* At 0°C, charge limit = 0 (below 5°C ramp) */
    TEST_ASSERT_EQ(chg, 0);
    /* Discharge at 0°C = 2C = 256000 mA */
    TEST_ASSERT_EQ(dchg, 256000);
}

/* ── Test: high SoC — charge derated ───────────────────────────────── */
static void test_high_soc_charge_derated(void)
{
    int32_t chg, dchg;
    setup_nominal();
    s_pack.soc_hundredths = 9500U; /* 95% */

    bms_current_limit_compute(&s_pack, &chg, &dchg);

    /* SoC charge at 95% = 1C = 128000 mA */
    TEST_ASSERT_EQ(chg, 128000);
}

/* ── Test: full SoC = 100% — charge = 0.5C ────────────────────────── */
static void test_full_soc(void)
{
    int32_t chg, dchg;
    setup_nominal();
    s_pack.soc_hundredths = 10000U; /* 100% */

    bms_current_limit_compute(&s_pack, &chg, &dchg);

    /* SoC charge at 100% = 0.5C = 64000 mA */
    TEST_ASSERT_EQ(chg, 64000);
    (void)dchg;
}

/* ── Test: high cell voltage — SEV limits charge ───────────────────── */
static void test_high_sev_charge(void)
{
    int32_t chg, dchg;
    setup_nominal();
    s_pack.max_cell_mv = 4150U; /* between 4100 and 4200 */

    bms_current_limit_compute(&s_pack, &chg, &dchg);

    /* SEV charge at 4150: interp between 4100→3C and 4200→0C = 1.5C = 192000 */
    TEST_ASSERT_EQ(chg, 192000);
    (void)dchg;
}

/* ── Test: low cell voltage — SEV limits discharge ─────────────────── */
static void test_low_sev_discharge(void)
{
    int32_t chg, dchg;
    setup_nominal();
    s_pack.min_cell_mv = 3100U; /* between 3000 and 3200 */

    bms_current_limit_compute(&s_pack, &chg, &dchg);

    /* SEV discharge at 3100: interp 3000→0C, 3200→0C = 0 */
    TEST_ASSERT_EQ(dchg, 0);
    (void)chg;
}

/* ── Test: min-of-three logic ──────────────────────────────────────── */
static void test_min_of_three(void)
{
    int32_t chg, dchg;
    setup_nominal();

    /* Set conditions where each source gives different limits */
    s_pack.max_temp_deci_c = 450; /* 45°C: charge=2C, discharge~3.8C */
    s_pack.soc_hundredths = 9000U; /* 90%: charge=2C, discharge=5C */
    s_pack.max_cell_mv = 4100U;   /* SEV charge=3C */
    s_pack.min_cell_mv = 3675U;   /* SEV discharge=5C */

    bms_current_limit_compute(&s_pack, &chg, &dchg);

    /* Charge: min(2C_temp, 2C_soc, 3C_sev) = 2C = 256000 */
    TEST_ASSERT_EQ(chg, 256000);
    /* Discharge: min(3.8C_temp, 5C_soc, 5C_sev) = 3.8C = 486400 */
    TEST_ASSERT_EQ(dchg, 486400);
}

/* ── Test: extreme cold — everything near zero ─────────────────────── */
static void test_extreme_cold(void)
{
    int32_t chg, dchg;
    setup_nominal();
    s_pack.max_temp_deci_c = -250; /* -25°C */

    bms_current_limit_compute(&s_pack, &chg, &dchg);

    TEST_ASSERT_EQ(chg, 0);
    /* Discharge at -25°C = 0.2C = 25600 */
    TEST_ASSERT_EQ(dchg, 25600);
}

/* ── Test: interpolation midpoint — temp charge ramp ───────────────── */
static void test_temp_charge_midpoint(void)
{
    int32_t chg, dchg;
    setup_nominal();
    /* Midpoint of 5°C→15°C charge ramp: 10°C should give 1.5C */
    s_pack.max_temp_deci_c = 100; /* 10°C = 100 deci-C */

    bms_current_limit_compute(&s_pack, &chg, &dchg);

    /* 10°C is midpoint of 50→150 deci-C range, 0C→3C = 1.5C = 192000 */
    TEST_ASSERT_EQ(chg, 192000);
    (void)dchg;
}

void test_current_limit_suite(void)
{
    test_nominal_full_limits();
    test_cold_temp_charge_zero();
    test_high_soc_charge_derated();
    test_full_soc();
    test_high_sev_charge();
    test_low_sev_discharge();
    test_min_of_three();
    test_extreme_cold();
    test_temp_charge_midpoint();
}
