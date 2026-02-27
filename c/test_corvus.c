/**
 * test_corvus.c -- Unit tests for Corvus Orca ESS BMS
 *
 * Simple assert-based framework, no external test library needed.
 */

#include "corvus_bms.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT_MSG(cond, fmt, ...) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        printf("  FAIL [%s:%d]: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (0)

#define ASSERT_NEAR(actual, expected, tol, name) \
    ASSERT_MSG(fabs((actual) - (expected)) <= (tol), \
               "%s: expected %.6f, got %.6f (tol %.6f)", \
               name, (double)(expected), (double)(actual), (double)(tol))

#define ASSERT_TRUE(cond, name) \
    ASSERT_MSG(cond, "%s: expected true", name)

#define ASSERT_FALSE(cond, name) \
    ASSERT_MSG(!(cond), "%s: expected false", name)

#define ASSERT_EQ_INT(actual, expected, name) \
    ASSERT_MSG((actual) == (expected), "%s: expected %d, got %d", \
               name, (int)(expected), (int)(actual))

/* =====================================================================
 * TEST: OCV curve at known SoC values
 * ===================================================================== */
static void test_ocv_curve(void)
{
    printf("test_ocv_curve\n");

    /* Exact breakpoints from the table */
    ASSERT_NEAR(corvus_ocv_from_soc(0.00), 3.000, 1e-6, "OCV at SoC=0%");
    ASSERT_NEAR(corvus_ocv_from_soc(0.50), 3.675, 1e-6, "OCV at SoC=50%");
    ASSERT_NEAR(corvus_ocv_from_soc(1.00), 4.190, 1e-6, "OCV at SoC=100%");
    ASSERT_NEAR(corvus_ocv_from_soc(0.20), 3.590, 1e-6, "OCV at SoC=20%");

    /* Interpolated mid-point between SoC=0.50 (3.675) and SoC=0.55 (3.690) */
    double expected = (3.675 + 3.690) / 2.0;
    ASSERT_NEAR(corvus_ocv_from_soc(0.525), expected, 1e-4, "OCV interpolated at 52.5%");

    /* Clamped at boundaries */
    ASSERT_NEAR(corvus_ocv_from_soc(-0.1), 3.000, 1e-6, "OCV clamped below 0");
    ASSERT_NEAR(corvus_ocv_from_soc(1.5),  4.190, 1e-6, "OCV clamped above 1");
}

/* =====================================================================
 * TEST: Resistance lookup at known (SoC, T) points
 * ===================================================================== */
static void test_resistance_lookup(void)
{
    printf("test_resistance_lookup\n");

    /* Exact table point: SoC=50%, T=25°C → 3.1 mΩ/module (U-shape minimum) */
    double r_mod = corvus_module_resistance(25.0, 0.50);
    ASSERT_NEAR(r_mod * 1e3, 3.1, 0.01, "R_module at 25C/50%");

    /* Pack = 22 modules in series */
    double r_pack = corvus_pack_resistance(25.0, 0.50);
    ASSERT_NEAR(r_pack * 1e3, 3.1 * 22.0, 0.3, "R_pack at 25C/50%");

    /* Cold: SoC=5%, T=-10°C → 15.3 mΩ/module */
    r_mod = corvus_module_resistance(-10.0, 0.05);
    ASSERT_NEAR(r_mod * 1e3, 15.3, 0.01, "R_module at -10C/5%");

    /* Interpolated: T=17.5°C (midpoint 10-25), SoC=50% → mid(4.0, 3.1)=3.55 */
    r_mod = corvus_module_resistance(17.5, 0.50);
    ASSERT_NEAR(r_mod * 1e3, 3.55, 0.01, "R_module interpolated at 17.5C/50%");
}

/* =====================================================================
 * TEST: Pack voltage under load (verify IR drop)
 * ===================================================================== */
static void test_pack_voltage_under_load(void)
{
    printf("test_pack_voltage_under_load\n");

    corvus_pack_t pack;
    corvus_pack_init(&pack, 1, 0.50, 25.0);

    /* At zero current, pack_voltage = OCV * N_cells */
    double ocv = corvus_ocv_from_soc(0.50);
    double expected_v = ocv * BMS_NUM_CELLS_SERIES;
    ASSERT_NEAR(pack.pack_voltage, expected_v, 0.1, "Pack V at zero current");

    /* Apply 100A charging current */
    corvus_pack_step(&pack, 1.0, 100.0, true, 0.0);
    /* V = OCV + I*R, so pack voltage should be HIGHER than OCV*N when charging */
    double r_pack = corvus_pack_resistance(pack.temperature, pack.soc);
    /* IR drop across pack */
    double ir_drop = 100.0 * r_pack;
    ASSERT_TRUE(pack.pack_voltage > expected_v, "Pack V rises with charge current");
    ASSERT_NEAR(pack.pack_voltage - expected_v, ir_drop, 1.0,
                "IR drop magnitude (approx)");
}

/* =====================================================================
 * TEST: State machine transitions (READY→CONNECTING→CONNECTED→FAULT→READY)
 * ===================================================================== */
static void test_state_machine_transitions(void)
{
    printf("test_state_machine_transitions\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);

    /* Initial state: READY */
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_READY, "Initial mode = READY");

    /* Request connect → CONNECTING */
    double bus_v = ctrl.pack.pack_voltage;
    bool ok = corvus_controller_request_connect(&ctrl, bus_v, true);
    ASSERT_TRUE(ok, "Connect request accepted");
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_CONNECTING, "Mode = CONNECTING");

    /* Step through pre-charge (5s) */
    for (int i = 0; i < 6; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_CONNECTED, "Mode = CONNECTED after precharge");
    ASSERT_TRUE(ctrl.contactors_closed, "Contactors closed");

    /* Trigger fault by forcing cell voltage high */
    ctrl.pack.cell_voltage = BMS_SE_OVER_VOLTAGE_FAULT + 0.01;
    for (int i = 0; i < 6; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_FAULT, "Mode = FAULT after OV");
    ASSERT_TRUE(ctrl.fault_latched, "Fault is latched");
    ASSERT_FALSE(ctrl.contactors_closed, "Contactors opened on fault");

    /* Reset: bring voltage back, accumulate safe time */
    ctrl.pack.cell_voltage = 3.7;
    ctrl.pack.temperature = 25.0;
    /* Need to accumulate 60s safe state via controller step */
    for (int i = 0; i < 65; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);

    ok = corvus_controller_manual_fault_reset(&ctrl);
    ASSERT_TRUE(ok, "Fault reset succeeds after 60s hold");
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_READY, "Mode = READY after reset");
}

/* =====================================================================
 * TEST: Alarm thresholds trigger at correct values/delays
 * ===================================================================== */
static void test_alarm_thresholds(void)
{
    printf("test_alarm_thresholds\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);

    double bus_v = ctrl.pack.pack_voltage;

    /* Over-temperature warning: set temp to 61°C, step for 6s */
    ctrl.pack.temperature = 61.0;
    for (int i = 0; i < 4; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_FALSE(ctrl.has_warning, "No OT warning before 5s delay");

    corvus_controller_step(&ctrl, 1.0, bus_v);
    corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_TRUE(ctrl.has_warning, "OT warning after 5+s");
    ASSERT_TRUE(strstr(ctrl.warning_message, "OT") != NULL, "Warning message contains OT");
}

/* =====================================================================
 * TEST: HW safety fires even when fault_latched
 * ===================================================================== */
static void test_hw_safety_independent(void)
{
    printf("test_hw_safety_independent\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);

    double bus_v = ctrl.pack.pack_voltage;

    /* First, latch a SW fault */
    ctrl.pack.cell_voltage = BMS_SE_OVER_VOLTAGE_FAULT + 0.01;
    for (int i = 0; i < 6; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_TRUE(ctrl.fault_latched, "SW fault latched");
    ASSERT_FALSE(ctrl.hw_fault_latched, "No HW fault yet");

    /* Now push to HW safety threshold */
    ctrl.pack.cell_voltage = BMS_HW_SAFETY_OVER_VOLTAGE + 0.01;
    for (int i = 0; i < 2; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_TRUE(ctrl.hw_fault_latched, "HW fault fires despite existing SW fault");
    ASSERT_TRUE(strstr(ctrl.fault_message, "HW SAFETY") != NULL,
                "Fault message contains HW SAFETY");
}

/* =====================================================================
 * TEST: Kirchhoff solver: 3 packs, verify ΣI_k = I_load
 * ===================================================================== */
static void test_kirchhoff_solver(void)
{
    printf("test_kirchhoff_solver\n");

    int    ids[]   = { 1, 2, 3 };
    double socs[]  = { 0.45, 0.55, 0.65 };
    double temps[] = { 25.0, 25.0, 25.0 };

    corvus_array_t array;
    corvus_array_init(&array, 3, ids, socs, temps);
    corvus_array_update_bus_voltage(&array);

    /* Connect all packs */
    corvus_array_connect_first(&array, true);
    for (int i = 0; i < 10; i++)
        corvus_array_step(&array, 1.0, 0.0, NULL);
    corvus_array_connect_remaining(&array, true);
    for (int i = 0; i < 10; i++)
        corvus_array_step(&array, 1.0, 0.0, NULL);

    /* Verify all connected */
    int num_conn = 0;
    for (int i = 0; i < 3; i++)
        if (array.controllers[i].mode == BMS_MODE_CONNECTED)
            num_conn++;
    ASSERT_EQ_INT(num_conn, 3, "All 3 packs connected");

    /* Apply 200A charge and step */
    corvus_array_step(&array, 1.0, 200.0, NULL);

    /* Verify KCL: sum of pack currents ≈ 200A */
    double sum_i = 0.0;
    for (int i = 0; i < 3; i++)
        sum_i += array.controllers[i].pack.current;
    ASSERT_NEAR(sum_i, 200.0, 2.0, "KCL: sum(I_k) = I_load");

    /* Packs have different SoC → different OCV → uneven distribution.
     * Lower-SoC packs absorb more current; highest-SoC pack may discharge.
     * Just verify each pack's current is within physical bounds. */
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(fabs(array.controllers[i].pack.current) < 300.0,
                    "Pack current within physical bounds");
    }
}

/* =====================================================================
 * TEST: Equalization: 3 packs at different SoC, verify ΣI_k ≈ 0
 * ===================================================================== */
static void test_equalization(void)
{
    printf("test_equalization\n");

    int    ids[]   = { 1, 2, 3 };
    double socs[]  = { 0.40, 0.50, 0.60 };
    double temps[] = { 25.0, 25.0, 25.0 };

    corvus_array_t array;
    corvus_array_init(&array, 3, ids, socs, temps);
    corvus_array_update_bus_voltage(&array);

    /* Connect all */
    corvus_array_connect_first(&array, true);
    for (int i = 0; i < 10; i++)
        corvus_array_step(&array, 1.0, 0.0, NULL);
    corvus_array_connect_remaining(&array, true);
    for (int i = 0; i < 10; i++)
        corvus_array_step(&array, 1.0, 0.0, NULL);

    /* Zero load → equalization */
    corvus_array_step(&array, 1.0, 0.0, NULL);

    double sum_i = 0.0;
    for (int i = 0; i < 3; i++)
        sum_i += array.controllers[i].pack.current;

    ASSERT_NEAR(sum_i, 0.0, 1.0, "Equalization: sum(I_k) ≈ 0");

    /* Pack with lowest SoC should be charging (positive current) */
    ASSERT_TRUE(array.controllers[0].pack.current > 0,
                "Low-SoC pack charges during equalization");
    /* Pack with highest SoC should be discharging (negative current) */
    ASSERT_TRUE(array.controllers[2].pack.current < 0,
                "High-SoC pack discharges during equalization");
}

/* =====================================================================
 * TEST: Current limits at boundary SoC/temp values
 * ===================================================================== */
static void test_current_limits_boundary(void)
{
    printf("test_current_limits_boundary\n");

    double cap = BMS_NOMINAL_CAPACITY_AH;

    /* At T=-25°C, charge should be zero (Figure 28) */
    bms_current_limit_t lim = corvus_temp_current_limit(-25.0, cap);
    ASSERT_NEAR(lim.charge, 0.0, 0.1, "Charge limit at -25C = 0");
    ASSERT_NEAR(lim.discharge, 0.2 * cap, 0.1, "Discharge limit at -25C");

    /* At T=25°C, charge should be 3.0C (Figure 28) */
    lim = corvus_temp_current_limit(25.0, cap);
    ASSERT_NEAR(lim.charge, 3.0 * cap, 0.1, "Charge limit at 25C");
    ASSERT_NEAR(lim.discharge, 5.0 * cap, 0.1, "Discharge limit at 25C");

    /* SoC=100%, charge should be 0.5C (Figure 29) */
    lim = corvus_soc_current_limit(1.0, cap);
    ASSERT_NEAR(lim.charge, 0.5 * cap, 0.1, "Charge limit at SoC=100%");

    /* SEV at 4.200V, charge should be 0 (Figure 30) */
    lim = corvus_sev_current_limit(4.200, cap);
    ASSERT_NEAR(lim.charge, 0.0, 0.1, "Charge limit at SEV=4.200V");
}

/* =====================================================================
 * TEST: Fault reset denied before hold time, accepted after
 * ===================================================================== */
static void test_fault_reset_hold_time(void)
{
    printf("test_fault_reset_hold_time\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);
    double bus_v = ctrl.pack.pack_voltage;

    /* Trigger a fault */
    ctrl.pack.cell_voltage = BMS_SE_OVER_VOLTAGE_FAULT + 0.01;
    for (int i = 0; i < 6; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_TRUE(ctrl.fault_latched, "Fault latched");

    /* Bring conditions back to safe */
    ctrl.pack.cell_voltage = 3.7;

    /* Step for 30s -- not enough for 60s hold */
    for (int i = 0; i < 30; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);

    bool result = corvus_controller_manual_fault_reset(&ctrl);
    ASSERT_FALSE(result, "Reset denied before 60s hold");
    ASSERT_TRUE(ctrl.fault_latched, "Still latched after denied reset");

    /* Step for 35 more seconds (total ~65s in safe state) */
    for (int i = 0; i < 35; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);

    result = corvus_controller_manual_fault_reset(&ctrl);
    ASSERT_TRUE(result, "Reset accepted after 60s hold");
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_READY, "Mode = READY after successful reset");
}

/* =====================================================================
 * TEST: Under-voltage fault triggers after delay
 * ===================================================================== */
static void test_under_voltage_fault(void)
{
    printf("test_under_voltage_fault\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);
    double bus_v = ctrl.pack.pack_voltage;

    /* Connect the pack */
    corvus_controller_request_connect(&ctrl, bus_v, true);
    for (int i = 0; i < 6; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_CONNECTED, "UV: pack connected");

    /* Force cell voltage below UV fault threshold */
    ctrl.pack.cell_voltage = BMS_SE_UNDER_VOLTAGE_FAULT - 0.01;

    /* 4s — not enough for 5s delay */
    for (int i = 0; i < 4; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_FALSE(ctrl.fault_latched, "UV fault not latched before 5s");

    /* 2 more seconds — should trigger */
    for (int i = 0; i < 2; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_TRUE(ctrl.fault_latched, "UV fault latched after 5+s");
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_FAULT, "Mode = FAULT after UV");
    ASSERT_TRUE(strstr(ctrl.fault_message, "UV") != NULL, "Fault message contains UV");
}

/* =====================================================================
 * TEST: Under-voltage warning with hysteresis clear
 * ===================================================================== */
static void test_under_voltage_warning(void)
{
    printf("test_under_voltage_warning\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);
    double bus_v = ctrl.pack.pack_voltage;

    /* Drive cell voltage to UV warning level (but above fault) */
    ctrl.pack.cell_voltage = 3.15;  /* below 3.200 warning, above 3.000 fault */

    for (int i = 0; i < 4; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_FALSE(ctrl.has_warning, "UV warning not yet at 4s");

    for (int i = 0; i < 2; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_TRUE(ctrl.has_warning, "UV warning active after 5+s");
    ASSERT_TRUE(strstr(ctrl.warning_message, "UV") != NULL, "Warning msg contains UV");

    /* Move voltage into deadband (above 3.200 but below 3.220) — warning should hold */
    ctrl.pack.cell_voltage = 3.21;
    for (int i = 0; i < 5; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_TRUE(ctrl.has_warning, "UV warning holds in deadband");

    /* Move above clear threshold (3.220) — warning should clear after hold time */
    ctrl.pack.cell_voltage = 3.25;
    for (int i = 0; i < 12; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_FALSE(ctrl.has_warning, "UV warning cleared after hysteresis+hold");
}

/* =====================================================================
 * TEST: Thermal model — heating and cooling
 * ===================================================================== */
static void test_thermal_model(void)
{
    printf("test_thermal_model\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, BMS_AMBIENT_TEMP);
    double bus_v = ctrl.pack.pack_voltage;

    /* Connect */
    corvus_controller_request_connect(&ctrl, bus_v, true);
    for (int i = 0; i < 6; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_CONNECTED, "Thermal: pack connected");

    double t_before = ctrl.pack.temperature;

    /* Discharge at -200A for 100 steps — should heat up via I²R + entropic heat */
    for (int i = 0; i < 100; i++)
        corvus_pack_step(&ctrl.pack, 1.0, -200.0, true, 0.0);

    ASSERT_TRUE(ctrl.pack.temperature > t_before + 0.01,
                "Temperature increased from I²R + entropic heating");

    double t_hot = ctrl.pack.temperature;

    /* Idle for 100 steps — should cool toward ambient */
    for (int i = 0; i < 100; i++)
        corvus_pack_step(&ctrl.pack, 1.0, 0.0, false, 0.0);

    ASSERT_TRUE(ctrl.pack.temperature < t_hot,
                "Temperature decreased when idle (cooling)");
    ASSERT_TRUE(ctrl.pack.temperature >= BMS_AMBIENT_TEMP - 0.5,
                "Temperature stays near or above ambient");
}

/* =====================================================================
 * TEST: Coulomb counting — 1hr at 128A ≈ 100% SoC change
 * ===================================================================== */
static void test_coulomb_counting(void)
{
    printf("test_coulomb_counting\n");

    corvus_pack_t pack;
    corvus_pack_init(&pack, 1, 0.0, 25.0);
    ASSERT_NEAR(pack.soc, 0.0, 1e-6, "Initial SoC = 0");

    /* Charge at 128A (1C) for 3600s = 1 hour */
    for (int i = 0; i < 3600; i++)
        corvus_pack_step(&pack, 1.0, 128.0, true, 0.0);

    /* Should be at or very near 100% (clamped to 1.0) */
    ASSERT_TRUE(pack.soc >= 0.99, "SoC >= 99% after 1hr at 1C");
    ASSERT_TRUE(pack.soc <= 1.0, "SoC clamped to 1.0");
}

/* =====================================================================
 * TEST: Disconnect — returns to READY with contactors open
 * ===================================================================== */
static void test_disconnect(void)
{
    printf("test_disconnect\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);
    double bus_v = ctrl.pack.pack_voltage;

    /* Connect */
    corvus_controller_request_connect(&ctrl, bus_v, true);
    for (int i = 0; i < 6; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_CONNECTED, "Disconnect: connected first");
    ASSERT_TRUE(ctrl.contactors_closed, "Contactors closed when connected");

    /* Disconnect */
    corvus_controller_request_disconnect(&ctrl);
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_READY, "Mode = READY after disconnect");
    ASSERT_FALSE(ctrl.contactors_closed, "Contactors open after disconnect");
}

/* =====================================================================
 * TEST: Connection rejection — bus voltage too far from pack voltage
 * ===================================================================== */
static void test_connection_rejection(void)
{
    printf("test_connection_rejection\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);

    /* Set bus voltage far from pack voltage */
    double far_bus_v = ctrl.pack.pack_voltage + 500.0;
    bool ok = corvus_controller_request_connect(&ctrl, far_bus_v, true);
    ASSERT_FALSE(ok, "Connect rejected when bus voltage too far");
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_READY, "Still READY after rejection");
}

/* =====================================================================
 * TEST: Overcurrent warning after 10s
 * ===================================================================== */
static void test_overcurrent_warning(void)
{
    printf("test_overcurrent_warning\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);
    double bus_v = ctrl.pack.pack_voltage;

    /* Connect */
    corvus_controller_request_connect(&ctrl, bus_v, true);
    for (int i = 0; i < 6; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_CONNECTED, "OC: pack connected");

    /* Compute OC threshold */
    bms_current_limit_t tc = corvus_temp_current_limit(25.0, BMS_NOMINAL_CAPACITY_AH);
    double oc_current = 1.05 * tc.charge + 5.0 + 20.0;

    /* Force pack current above OC threshold */
    ctrl.pack.current = oc_current;

    for (int i = 0; i < 9; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_FALSE(ctrl.has_warning, "OC warning not yet at 9s");

    for (int i = 0; i < 2; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_TRUE(ctrl.has_warning, "OC warning active after 10+s");
    ASSERT_TRUE(strstr(ctrl.warning_message, "OC") != NULL, "Warning contains OC");
}

/* =====================================================================
 * TEST: Warning hysteresis clear — OT warning holds then clears
 * ===================================================================== */
static void test_warning_hysteresis_clear(void)
{
    printf("test_warning_hysteresis_clear\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);
    double bus_v = ctrl.pack.pack_voltage;

    /* Trigger OT warning */
    ctrl.pack.temperature = 61.0;
    for (int i = 0; i < 6; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_TRUE(ctrl.has_warning, "OT warning triggered");

    /* Cool to 57°C (below OT_WARN_CLEAR = 57.0°C → at boundary, timer should NOT reset) */
    /* Actually need to go below 57.0 for the < check */
    ctrl.pack.temperature = 56.9;

    /* Warning should hold for WARNING_HOLD_TIME (10s) */
    for (int i = 0; i < 9; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_TRUE(ctrl.has_warning, "OT warning holds during hold time");

    /* After 10+ more seconds, should clear */
    for (int i = 0; i < 3; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);
    ASSERT_FALSE(ctrl.has_warning, "OT warning cleared after hold time");
}

/* =====================================================================
 * TEST: Array current limits = min(per-pack) × N
 * ===================================================================== */
static void test_array_current_limits(void)
{
    printf("test_array_current_limits\n");

    int    ids[]   = { 1, 2, 3 };
    double socs[]  = { 0.50, 0.50, 0.50 };
    /* Different temps → different per-pack limits */
    double temps[] = { 25.0, 35.0, 42.0 };

    corvus_array_t array;
    corvus_array_init(&array, 3, ids, socs, temps);
    corvus_array_update_bus_voltage(&array);

    /* Connect all packs */
    corvus_array_connect_first(&array, true);
    for (int i = 0; i < 10; i++)
        corvus_array_step(&array, 1.0, 0.0, NULL);
    corvus_array_connect_remaining(&array, true);
    for (int i = 0; i < 10; i++)
        corvus_array_step(&array, 1.0, 0.0, NULL);

    int num_conn = 0;
    for (int i = 0; i < 3; i++)
        if (array.controllers[i].mode == BMS_MODE_CONNECTED)
            num_conn++;
    ASSERT_EQ_INT(num_conn, 3, "Array limits: all 3 connected");

    /* Find min per-pack charge limit */
    double min_charge = 1e30, min_disch = 1e30;
    for (int i = 0; i < 3; i++) {
        if (array.controllers[i].charge_current_limit < min_charge)
            min_charge = array.controllers[i].charge_current_limit;
        if (array.controllers[i].discharge_current_limit < min_disch)
            min_disch = array.controllers[i].discharge_current_limit;
    }

    ASSERT_NEAR(array.array_charge_limit, min_charge * 3, 1.0,
                "Array charge limit = min×N");
    ASSERT_NEAR(array.array_discharge_limit, min_disch * 3, 1.0,
                "Array discharge limit = min×N");
}

/* =====================================================================
 * TEST: Fractional dt — timer accumulation with dt=0.1
 * ===================================================================== */
static void test_fractional_dt(void)
{
    printf("test_fractional_dt\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);
    double bus_v = ctrl.pack.pack_voltage;

    /* Trigger OT warning with fractional dt */
    ctrl.pack.temperature = 61.0;

    /* 52 steps × 0.1s = 5.2s — should trigger warning (5s delay) */
    for (int i = 0; i < 52; i++)
        corvus_controller_step(&ctrl, 0.1, bus_v);
    ASSERT_TRUE(ctrl.has_warning, "OT warning with dt=0.1 after 5s");

    /* Verify SoC accumulation with fractional dt */
    corvus_pack_t pack;
    corvus_pack_init(&pack, 1, 0.50, 25.0);

    double soc_before = pack.soc;
    /* 50 steps × 0.1s × 128A = 640 As = 0.1778 Ah → ΔSoC ≈ 0.00139 */
    for (int i = 0; i < 50; i++)
        corvus_pack_step(&pack, 0.1, 128.0, true, 0.0);

    double expected_delta = (128.0 * 5.0) / (BMS_NOMINAL_CAPACITY_AH * 3600.0);
    ASSERT_NEAR(pack.soc - soc_before, expected_delta, 1e-5,
                "SoC delta correct with fractional dt");
}

/* =====================================================================
 * TEST: Pack ID uniqueness validation
 * ===================================================================== */
static void test_pack_id_uniqueness(void)
{
    printf("test_pack_id_uniqueness\n");

    int unique_ids[] = { 1, 2, 3 };
    ASSERT_TRUE(corvus_validate_unique_pack_ids(unique_ids, 3),
                "Unique IDs accepted");

    int dup_ids[] = { 1, 2, 1 };
    ASSERT_FALSE(corvus_validate_unique_pack_ids(dup_ids, 3),
                 "Duplicate IDs rejected");

    ASSERT_TRUE(corvus_validate_unique_pack_ids(NULL, 0),
                "Empty array accepted");

    int single[] = { 42 };
    ASSERT_TRUE(corvus_validate_unique_pack_ids(single, 1),
                "Single ID accepted");
}

/* =====================================================================
 * TEST: Input validation — SoC clamped, dt=0 is no-op
 * ===================================================================== */
static void test_input_validation(void)
{
    printf("test_input_validation\n");

    corvus_pack_t pack;
    /* SoC out of range should be clamped */
    corvus_pack_init(&pack, 99, 1.5, 25.0);
    ASSERT_NEAR(pack.soc, 1.0, 1e-9, "SoC clamped to 1.0");

    corvus_pack_init(&pack, 100, -0.5, 25.0);
    ASSERT_NEAR(pack.soc, 0.0, 1e-9, "SoC clamped to 0.0");

    /* dt=0 should return -1 (error) and not modify state */
    corvus_pack_init(&pack, 101, 0.5, 25.0);
    double soc_before = pack.soc;
    int rc = corvus_pack_step(&pack, 0.0, 100.0, true, 0.0);
    ASSERT_EQ_INT(rc, -1, "dt=0 returns -1");
    ASSERT_NEAR(pack.soc, soc_before, 1e-9, "dt=0 does not modify SoC");

    /* dt<0 should also return -1 */
    rc = corvus_pack_step(&pack, -1.0, 100.0, true, 0.0);
    ASSERT_EQ_INT(rc, -1, "dt<0 returns -1");
    ASSERT_NEAR(pack.soc, soc_before, 1e-9, "dt<0 does not modify SoC");
}

/* =====================================================================
 * TEST: Leaky fault timer decay
 * ===================================================================== */
static void test_leaky_timer_decay(void)
{
    printf("test_leaky_timer_decay\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);
    double bus_v = ctrl.pack.pack_voltage;

    /* Force OV fault condition for 3s to build up timer */
    ctrl.pack.cell_voltage = BMS_SE_OVER_VOLTAGE_FAULT + 0.01;
    for (int i = 0; i < 3; i++)
        corvus_controller_step(&ctrl, 1.0, bus_v);

    double timer_after_active = ctrl.ov_fault_timer;
    ASSERT_NEAR(timer_after_active, 3.0, 0.01, "OV timer = 3.0 after 3s active");

    /* Clear condition for 1s -- timer should decay, not reset to 0 */
    ctrl.pack.cell_voltage = 3.7;
    corvus_controller_step(&ctrl, 1.0, bus_v);

    ASSERT_TRUE(ctrl.ov_fault_timer > 0.0, "Leaky timer > 0 after 1s clear");
    ASSERT_TRUE(ctrl.ov_fault_timer < timer_after_active,
                "Leaky timer decreased after clear");
    /* Expected: 3.0 - 1.0 * 0.5 = 2.5 */
    ASSERT_NEAR(ctrl.ov_fault_timer, 2.5, 0.01, "Leaky timer = 2.5 after 1s decay");
}

/* =====================================================================
 * TEST: Entropic heating sign -- discharge at mid-SoC should add heat
 * ===================================================================== */
static void test_entropic_heating_sign(void)
{
    printf("test_entropic_heating_sign\n");

    /* At mid-SoC (0.5), dOCV/dT = -0.35 mV/K (7-segment curve)
     * Discharging (I < 0): Q_rev = I * T_K * dOCV/dT * n_cells
     * I < 0, dOCV/dT < 0 → Q_rev > 0 (exothermic, adds heat) */
    double docv = corvus_docv_dt(0.5);
    ASSERT_NEAR(docv, -0.35e-3, 1e-6, "dOCV/dT at SoC=0.5");

    /* Compare two packs: one with entropic heating (discharge), one idle */
    corvus_pack_t pack_disch, pack_idle;
    corvus_pack_init(&pack_disch, 1, 0.50, 25.0);
    corvus_pack_init(&pack_idle, 2, 0.50, 25.0);

    /* Discharge at -100A for 100s */
    for (int i = 0; i < 100; i++) {
        corvus_pack_step(&pack_disch, 1.0, -100.0, true, 0.0);
        corvus_pack_step(&pack_idle, 1.0, 0.0, false, 0.0);
    }

    /* Discharging pack should be warmer than idle (I²R + positive Q_rev) */
    ASSERT_TRUE(pack_disch.temperature > pack_idle.temperature,
                "Discharging pack warmer than idle");

    /* At high SoC (0.9), dOCV/dT = +0.05 mV/K (7-segment: 0.85-0.95 range) */
    double docv_high = corvus_docv_dt(0.9);
    ASSERT_NEAR(docv_high, 0.05e-3, 1e-6, "dOCV/dT at SoC=0.9 is positive");
}

/* =====================================================================
 * TEST: Large-dt subdivision -- result should match many small steps
 * ===================================================================== */
static void test_large_dt_subdivision(void)
{
    printf("test_large_dt_subdivision\n");

    corvus_pack_t pack_big, pack_small;
    corvus_pack_init(&pack_big, 1, 0.50, 25.0);
    corvus_pack_init(&pack_small, 2, 0.50, 25.0);

    /* One big step of 30s should be subdivided into 3×10s */
    corvus_pack_step(&pack_big, 30.0, 100.0, true, 0.0);

    /* Three small steps of 10s each */
    corvus_pack_step(&pack_small, 10.0, 100.0, true, 0.0);
    corvus_pack_step(&pack_small, 10.0, 100.0, true, 0.0);
    corvus_pack_step(&pack_small, 10.0, 100.0, true, 0.0);

    ASSERT_NEAR(pack_big.soc, pack_small.soc, 1e-6,
                "Large-dt SoC matches 3x small-dt");
    ASSERT_NEAR(pack_big.temperature, pack_small.temperature, 0.01,
                "Large-dt temp matches 3x small-dt");
}

/* =====================================================================
 * TEST: Max temperature clamp at 200°C
 * ===================================================================== */
static void test_max_temperature_clamp(void)
{
    printf("test_max_temperature_clamp\n");

    corvus_pack_t pack;
    corvus_pack_init(&pack, 1, 0.50, 190.0);

    /* Inject massive external heat to push past 200°C */
    corvus_pack_step(&pack, 1.0, 0.0, false, 1e9);

    ASSERT_TRUE(pack.temperature <= BMS_MAX_TEMPERATURE + 0.01,
                "Temperature clamped at MAX_TEMPERATURE");
    ASSERT_NEAR(pack.temperature, BMS_MAX_TEMPERATURE, 0.01,
                "Temperature = 200.0 after clamp");
}

/* =====================================================================
 * TEST: dt <= 0 returns error code
 * ===================================================================== */
static void test_dt_error_code(void)
{
    printf("test_dt_error_code\n");

    corvus_pack_t pack;
    corvus_pack_init(&pack, 1, 0.5, 25.0);

    int rc = corvus_pack_step(&pack, 0.0, 100.0, true, 0.0);
    ASSERT_EQ_INT(rc, -1, "dt=0 returns -1");

    rc = corvus_pack_step(&pack, -5.0, 100.0, true, 0.0);
    ASSERT_EQ_INT(rc, -1, "dt=-5 returns -1");

    rc = corvus_pack_step(&pack, 1.0, 100.0, true, 0.0);
    ASSERT_EQ_INT(rc, 0, "dt=1 returns 0 (success)");
}

/* =====================================================================
 * TEST: Oscillating OV fault -- leaky timer eventually trips
 * ===================================================================== */
static void test_oscillating_ov_fault(void)
{
    printf("test_oscillating_ov_fault\n");

    corvus_controller_t ctrl;
    corvus_controller_init(&ctrl, 1, 0.50, 25.0);
    double bus_v = ctrl.pack.pack_voltage;

    /* Alternate: 2s above OV fault threshold, 2s just below, repeat 20 times.
     * Each 2s above adds 2.0 to timer. Each 2s below decays by 2*0.5=1.0.
     * Net gain per cycle: +1.0. After 5 cycles, timer reaches 5.0 → fault. */
    bool faulted = false;
    for (int cycle = 0; cycle < 20 && !faulted; cycle++) {
        /* 2s above threshold */
        ctrl.pack.cell_voltage = BMS_SE_OVER_VOLTAGE_FAULT + 0.01;
        for (int i = 0; i < 2; i++) {
            corvus_controller_step(&ctrl, 1.0, bus_v);
            if (ctrl.fault_latched) { faulted = true; break; }
        }
        if (faulted) break;

        /* 2s below threshold */
        ctrl.pack.cell_voltage = BMS_SE_OVER_VOLTAGE_FAULT - 0.01;
        for (int i = 0; i < 2; i++) {
            corvus_controller_step(&ctrl, 1.0, bus_v);
            if (ctrl.fault_latched) { faulted = true; break; }
        }
    }

    ASSERT_TRUE(faulted, "Oscillating OV eventually trips fault via leaky timer");
    ASSERT_EQ_INT(ctrl.mode, BMS_MODE_FAULT, "Mode = FAULT after oscillating OV");
    ASSERT_TRUE(strstr(ctrl.fault_message, "OV") != NULL,
                "Fault message contains OV");
}

/* =====================================================================
 * TEST: corvus_array_find_pack_index helper
 * ===================================================================== */
static void test_find_pack_index(void)
{
    printf("test_find_pack_index\n");

    int    ids[]   = { 10, 20, 30 };
    double socs[]  = { 0.5, 0.5, 0.5 };
    double temps[] = { 25.0, 25.0, 25.0 };

    corvus_array_t array;
    corvus_array_init(&array, 3, ids, socs, temps);

    ASSERT_EQ_INT(corvus_array_find_pack_index(&array, 10), 0, "pack_id 10 at index 0");
    ASSERT_EQ_INT(corvus_array_find_pack_index(&array, 20), 1, "pack_id 20 at index 1");
    ASSERT_EQ_INT(corvus_array_find_pack_index(&array, 30), 2, "pack_id 30 at index 2");
    ASSERT_EQ_INT(corvus_array_find_pack_index(&array, 99), -1, "pack_id 99 not found");
}

/* =====================================================================
 * MAIN
 * ===================================================================== */
int main(void)
{
    printf("========================================\n");
    printf("  Corvus BMS Unit Tests\n");
    printf("========================================\n\n");

    test_ocv_curve();
    test_resistance_lookup();
    test_pack_voltage_under_load();
    test_state_machine_transitions();
    test_alarm_thresholds();
    test_hw_safety_independent();
    test_kirchhoff_solver();
    test_equalization();
    test_current_limits_boundary();
    test_fault_reset_hold_time();
    test_under_voltage_fault();
    test_under_voltage_warning();
    test_thermal_model();
    test_coulomb_counting();
    test_disconnect();
    test_connection_rejection();
    test_overcurrent_warning();
    test_warning_hysteresis_clear();
    test_array_current_limits();
    test_fractional_dt();
    test_pack_id_uniqueness();
    test_input_validation();
    test_leaky_timer_decay();
    test_entropic_heating_sign();
    test_large_dt_subdivision();
    test_max_temperature_clamp();
    test_dt_error_code();
    test_oscillating_ov_fault();
    test_find_pack_index();

    printf("\n========================================\n");
    printf("  Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf("\n========================================\n");

    return g_tests_failed > 0 ? 1 : 0;
}
