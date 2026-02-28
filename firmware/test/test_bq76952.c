/**
 * test_bq76952.c — BQ76952 driver tests via mock HAL
 */

#include "bms_bq76952.h"
#include "bms_hal.h"
#include "bms_config.h"
#include <stdio.h>
#include <string.h>

/* Import test macros from test_main.c */
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

/* Mock control functions */
extern void mock_hal_reset(void);
extern void mock_set_cell_voltage(uint8_t module_id, uint8_t cell_idx, uint16_t mv);
extern void mock_set_all_cell_voltages(uint16_t mv);
extern void mock_set_temperature(uint8_t module_id, uint8_t sensor_idx, int16_t deci_c);
extern void mock_set_i2c_fail(bool fail);
extern void mock_set_safety_a(uint8_t module_id, uint8_t flags);

/* ── Tests ─────────────────────────────────────────────────────────── */

static void test_init_success(void)
{
    mock_hal_reset();
    TEST_ASSERT_EQ(bq76952_init(0), 0);
}

static void test_init_i2c_failure(void)
{
    mock_hal_reset();
    mock_set_i2c_fail(true);
    TEST_ASSERT(bq76952_init(0) != 0);
    mock_set_i2c_fail(false);
}

static void test_read_cell_voltage(void)
{
    mock_hal_reset();
    mock_set_cell_voltage(0, 0, 3675U);
    mock_set_cell_voltage(0, 5, 4100U);

    uint16_t v0 = bq76952_read_cell_voltage(0, 0);
    uint16_t v5 = bq76952_read_cell_voltage(0, 5);

    TEST_ASSERT_EQ(v0, 3675U);
    TEST_ASSERT_EQ(v5, 4100U);
}

static void test_read_cell_out_of_range(void)
{
    mock_hal_reset();
    /* Cell index >= BMS_SE_PER_MODULE should return 0 */
    uint16_t v = bq76952_read_cell_voltage(0, BMS_SE_PER_MODULE);
    TEST_ASSERT_EQ(v, 0U);
}

static void test_read_all_cells(void)
{
    mock_hal_reset();
    uint16_t cells[BMS_SE_PER_MODULE];
    uint8_t i;

    /* Set ascending voltages */
    for (i = 0U; i < BMS_SE_PER_MODULE; i++) {
        mock_set_cell_voltage(0, i, (uint16_t)(3600U + i * 10U));
    }

    int32_t rc = bq76952_read_all_cells(0, cells);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ(cells[0], 3600U);
    TEST_ASSERT_EQ(cells[13], 3730U);
}

static void test_read_temperature(void)
{
    mock_hal_reset();
    /* Set 25.0°C */
    mock_set_temperature(0, 0, 250);

    int16_t t = bq76952_read_temperature(0, 0);
    /* Should return ~250 (25.0°C in deci-C) */
    /* There may be a ±1 rounding due to 2731 vs 2732 */
    TEST_ASSERT(t >= 249 && t <= 251);
}

static void test_read_safety(void)
{
    mock_hal_reset();
    mock_set_safety_a(0, BQ_SSA_CELL_OV | BQ_SSA_OC_CHG);

    bms_bq_safety_t safety;
    int32_t rc = bq76952_read_safety(0, &safety);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT(safety.safety_status_a & BQ_SSA_CELL_OV);
    TEST_ASSERT(safety.safety_status_a & BQ_SSA_OC_CHG);
    TEST_ASSERT(!(safety.safety_status_a & BQ_SSA_SC_DCHG));
}

static void test_checksum(void)
{
    /* Verify checksum computation: ~(sum of bytes) */
    uint8_t data[] = {0x10, 0x20, 0x30};
    uint8_t cksum = bq76952_compute_checksum(data, 3);
    /* sum = 0x60, ~0x60 = 0x9F */
    TEST_ASSERT_EQ(cksum, 0x9FU);
}

static void test_cell_reg_macro(void)
{
    /* Cell 0 → 0x14, Cell 1 → 0x16, Cell 13 → 0x2E */
    TEST_ASSERT_EQ(BQ76952_CELL_REG(0), 0x14U);
    TEST_ASSERT_EQ(BQ76952_CELL_REG(1), 0x16U);
    TEST_ASSERT_EQ(BQ76952_CELL_REG(13), 0x2EU);
}

void test_bq76952_suite(void)
{
    test_init_success();
    test_init_i2c_failure();
    test_read_cell_voltage();
    test_read_cell_out_of_range();
    test_read_all_cells();
    test_read_temperature();
    test_read_safety();
    test_checksum();
    test_cell_reg_macro();
}
