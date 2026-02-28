/**
 * test_can.c — CAN frame encode/decode tests
 */

#include "bms_can.h"
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

/* ── Test: encode status frame ─────────────────────────────────────── */
static void test_encode_status(void)
{
    bms_pack_data_t pack;
    bms_can_frame_t frame;

    memset(&pack, 0, sizeof(pack));
    pack.mode = BMS_MODE_CONNECTED;
    pack.pack_voltage_mv = 115000U;  /* 1150.00V → 1150 in 0.1V */
    pack.pack_current_ma = -50000;   /* -50A → -500 in 0.1A */
    pack.soc_hundredths = 7500U;     /* 75.00% → 75 */
    pack.max_temp_deci_c = 350;      /* 35.0°C → 35+40=75 */

    bms_can_encode_status(&pack, &frame);

    TEST_ASSERT_EQ(frame.id, (uint32_t)CAN_ID_ARRAY_STATUS);
    TEST_ASSERT_EQ(frame.dlc, 8U);
    TEST_ASSERT_EQ(frame.data[0], (uint8_t)BMS_MODE_CONNECTED);

    /* Voltage: 115000/100 = 1150 = 0x047E */
    TEST_ASSERT_EQ(frame.data[1], 0x04U);
    TEST_ASSERT_EQ(frame.data[2], 0x7EU);

    /* Current: -50000/100 = -500 = 0xFE0C (signed) */
    TEST_ASSERT_EQ(frame.data[3], 0xFEU);
    TEST_ASSERT_EQ(frame.data[4], 0x0CU);

    /* SoC: 7500/100 = 75 */
    TEST_ASSERT_EQ(frame.data[5], 75U);

    /* Temp: 350/10 + 40 = 75 */
    TEST_ASSERT_EQ(frame.data[6], 75U);
}

/* ── Test: encode voltage summary ──────────────────────────────────── */
static void test_encode_voltages(void)
{
    bms_pack_data_t pack;
    bms_can_frame_t frame;

    memset(&pack, 0, sizeof(pack));
    pack.max_cell_mv = 4100U;
    pack.min_cell_mv = 3600U;
    pack.avg_cell_mv = 3850U;

    bms_can_encode_voltages(&pack, &frame);

    TEST_ASSERT_EQ(frame.id, (uint32_t)CAN_ID_PACK_VOLTAGES);
    /* max: 4100 = 0x1004 */
    TEST_ASSERT_EQ(frame.data[0], 0x10U);
    TEST_ASSERT_EQ(frame.data[1], 0x04U);
    /* min: 3600 = 0x0E10 */
    TEST_ASSERT_EQ(frame.data[2], 0x0EU);
    TEST_ASSERT_EQ(frame.data[3], 0x10U);
    /* avg: 3850 = 0x0F0A */
    TEST_ASSERT_EQ(frame.data[4], 0x0FU);
    TEST_ASSERT_EQ(frame.data[5], 0x0AU);
    /* imbalance: 500 = 0x01F4 */
    TEST_ASSERT_EQ(frame.data[6], 0x01U);
    TEST_ASSERT_EQ(frame.data[7], 0xF4U);
}

/* ── Test: encode temperatures ─────────────────────────────────────── */
static void test_encode_temps(void)
{
    bms_pack_data_t pack;
    bms_can_frame_t frame;

    memset(&pack, 0, sizeof(pack));
    pack.max_temp_deci_c = 450;    /* 45.0°C */
    pack.min_temp_deci_c = 200;    /* 20.0°C */
    pack.charge_limit_ma = 384000; /* 384A → 3840 in 0.1A */
    pack.discharge_limit_ma = 640000; /* 640A → 6400 in 0.1A */

    bms_can_encode_temps(&pack, &frame);

    TEST_ASSERT_EQ(frame.id, (uint32_t)CAN_ID_PACK_TEMPS);
    /* max temp: 450 = 0x01C2 */
    TEST_ASSERT_EQ(frame.data[0], 0x01U);
    TEST_ASSERT_EQ(frame.data[1], 0xC2U);
    /* min temp: 200 = 0x00C8 */
    TEST_ASSERT_EQ(frame.data[2], 0x00U);
    TEST_ASSERT_EQ(frame.data[3], 0xC8U);
}

/* ── Test: decode EMS command ──────────────────────────────────────── */
static void test_decode_ems_command(void)
{
    bms_can_frame_t frame;
    bms_ems_command_t cmd;

    mock_hal_reset();

    memset(&frame, 0, sizeof(frame));
    frame.id = CAN_ID_EMS_COMMAND;
    frame.dlc = 5U;
    frame.data[0] = (uint8_t)EMS_CMD_SET_LIMITS;
    /* charge limit: 100A = 0x0064 */
    frame.data[1] = 0x00U;
    frame.data[2] = 0x64U;
    /* discharge limit: 200A = 0x00C8 */
    frame.data[3] = 0x00U;
    frame.data[4] = 0xC8U;

    int32_t rc = bms_can_decode_ems_command(&frame, &cmd);
    TEST_ASSERT_EQ(rc, 0);
    TEST_ASSERT_EQ((int)cmd.type, (int)EMS_CMD_SET_LIMITS);
    TEST_ASSERT_EQ(cmd.charge_limit_ma, 100000);
    TEST_ASSERT_EQ(cmd.discharge_limit_ma, 200000);
}

/* ── Test: decode rejects wrong ID ─────────────────────────────────── */
static void test_decode_wrong_id(void)
{
    bms_can_frame_t frame;
    bms_ems_command_t cmd;

    memset(&frame, 0, sizeof(frame));
    frame.id = CAN_ID_ARRAY_STATUS; /* wrong ID */
    frame.dlc = 5U;

    int32_t rc = bms_can_decode_ems_command(&frame, &cmd);
    TEST_ASSERT(rc < 0);
}

/* ── Test: decode rejects short DLC ────────────────────────────────── */
static void test_decode_short_dlc(void)
{
    bms_can_frame_t frame;
    bms_ems_command_t cmd;

    memset(&frame, 0, sizeof(frame));
    frame.id = CAN_ID_EMS_COMMAND;
    frame.dlc = 3U; /* too short */

    int32_t rc = bms_can_decode_ems_command(&frame, &cmd);
    TEST_ASSERT(rc < 0);
}

/* ── Test: decode rejects invalid command type ─────────────────────── */
static void test_decode_invalid_cmd(void)
{
    bms_can_frame_t frame;
    bms_ems_command_t cmd;

    mock_hal_reset();
    memset(&frame, 0, sizeof(frame));
    frame.id = CAN_ID_EMS_COMMAND;
    frame.dlc = 5U;
    frame.data[0] = 99U; /* invalid type */

    int32_t rc = bms_can_decode_ems_command(&frame, &cmd);
    TEST_ASSERT(rc < 0);
}

/* ── Test: encode heartbeat ────────────────────────────────────────── */
static void test_encode_heartbeat(void)
{
    bms_can_frame_t frame;

    bms_can_encode_heartbeat(0x12345678U, &frame);
    TEST_ASSERT_EQ(frame.data[0], 0x12U);
    TEST_ASSERT_EQ(frame.data[1], 0x34U);
    TEST_ASSERT_EQ(frame.data[2], 0x56U);
    TEST_ASSERT_EQ(frame.data[3], 0x78U);
}

void test_can_suite(void)
{
    test_encode_status();
    test_encode_voltages();
    test_encode_temps();
    test_decode_ems_command();
    test_decode_wrong_id();
    test_decode_short_dlc();
    test_decode_invalid_cmd();
    test_encode_heartbeat();
}
