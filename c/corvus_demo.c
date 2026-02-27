/**
 * corvus_demo.c -- Corvus Orca ESS Scenario Runner
 *
 * 8-phase demo with CSV output, matching the Python v4 scenario.
 *
 * Independent simulation of Orca ESS interface behaviors for integration
 * testing and educational purposes. Not affiliated with, endorsed by, or
 * derived from Corvus Energy's proprietary software.
 *
 * Reference: Corvus Energy Orca ESS integrator documentation
 *
 * LIMITATIONS:
 *   - HW safety is simulated in software, not an independent hardware protection layer
 *   - No watchdog timer, contactor welding detection, or feedback verification
 *   - No CAN/Modbus communication timeout modeling
 *   - No ground fault / insulation monitoring
 *   - No pre-charge inrush current modeling (timer only)
 *   - No cell balancing or per-cell monitoring (lumped single-cell model)
 *   - No aging, SOH, capacity fade, or calendar degradation
 *   - No self-discharge
 *   - Equalization currents may have small KCL residual after per-pack clamping
 *   - Array current limits use min×N per manual Section 7.4 example (conservative)
 *   - Warning hysteresis deadbands and fault reset hold time are engineering choices
 *   - Thermal model is lumped per-pack (no cell-to-cell thermal gradients)
 */

#include "corvus_bms.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define NUM_PACKS   3
_Static_assert(NUM_PACKS <= BMS_MAX_PACKS, "NUM_PACKS exceeds BMS_MAX_PACKS");
#define MAX_ROWS    3000

/* CSV row storage -- static allocation, no malloc */
typedef struct {
    double time;
    double bus_voltage;
    double array_charge_limit;
    double array_discharge_limit;
    double pack_soc[NUM_PACKS];
    double pack_voltage[NUM_PACKS];
    double pack_cell_v[NUM_PACKS];
    double pack_temp[NUM_PACKS];
    double pack_current[NUM_PACKS];
    double pack_charge_limit[NUM_PACKS];
    double pack_discharge_limit[NUM_PACKS];
    bms_pack_mode_t pack_mode[NUM_PACKS];
} csv_row_t;

static csv_row_t g_rows[MAX_ROWS];
static int       g_num_rows = 0;

static void record(double t, corvus_array_t *array)
{
    if (g_num_rows >= MAX_ROWS) return;
    csv_row_t *row = &g_rows[g_num_rows++];
    row->time = t;
    row->bus_voltage = array->bus_voltage;
    row->array_charge_limit = array->array_charge_limit;
    row->array_discharge_limit = array->array_discharge_limit;
    for (int i = 0; i < NUM_PACKS; i++) {
        corvus_controller_t *c = &array->controllers[i];
        row->pack_soc[i]             = c->pack.soc * 100.0;
        row->pack_voltage[i]         = c->pack.pack_voltage;
        row->pack_cell_v[i]          = c->pack.cell_voltage;
        row->pack_temp[i]            = c->pack.temperature;
        row->pack_current[i]         = c->pack.current;
        row->pack_charge_limit[i]    = c->charge_current_limit;
        row->pack_discharge_limit[i] = c->discharge_current_limit;
        row->pack_mode[i]            = c->mode;
    }
}

static void write_csv(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "Cannot open %s for writing\n", path); return; }

    fprintf(f, "time,bus_voltage,array_charge_limit,array_discharge_limit");
    for (int i = 0; i < NUM_PACKS; i++) {
        fprintf(f, ",pack%d_soc,pack%d_voltage,pack%d_cell_v,pack%d_temp,"
                   "pack%d_current,pack%d_charge_limit,pack%d_discharge_limit,"
                   "pack%d_mode",
                i+1, i+1, i+1, i+1, i+1, i+1, i+1, i+1);
    }
    fprintf(f, "\n");

    for (int r = 0; r < g_num_rows; r++) {
        csv_row_t *row = &g_rows[r];
        fprintf(f, "%.1f,%.2f,%.2f,%.2f",
                row->time, row->bus_voltage,
                row->array_charge_limit, row->array_discharge_limit);
        for (int i = 0; i < NUM_PACKS; i++) {
            fprintf(f, ",%.4f,%.2f,%.4f,%.2f,%.2f,%.2f,%.2f,%s",
                    row->pack_soc[i], row->pack_voltage[i],
                    row->pack_cell_v[i], row->pack_temp[i],
                    row->pack_current[i], row->pack_charge_limit[i],
                    row->pack_discharge_limit[i],
                    bms_mode_name(row->pack_mode[i]));
        }
        fprintf(f, "\n");
    }

    fclose(f);
    printf("[Output] CSV: %s (%d rows)\n", path, g_num_rows);
}

int main(void)
{
    printf("======================================================================\n");
    printf("  CORVUS ORCA ESS DEMO v4 (C port)\n");
    printf("  Reference: Corvus Energy Orca ESS integrator documentation\n");
    printf("======================================================================\n\n");

    /* Initialize 3-pack array */
    int    ids[]   = { 1, 2, 3 };
    double socs[]  = { 0.45, 0.55, 0.65 };
    double temps[] = { BMS_AMBIENT_TEMP, BMS_AMBIENT_TEMP, BMS_AMBIENT_TEMP };

    corvus_array_t array;
    corvus_array_init(&array, NUM_PACKS, ids, socs, temps);
    corvus_array_update_bus_voltage(&array);

    printf("  Pack voltages: [%.1fV, %.1fV, %.1fV]\n",
           array.controllers[0].pack.pack_voltage,
           array.controllers[1].pack.pack_voltage,
           array.controllers[2].pack.pack_voltage);
    printf("  Voltage match threshold: %.1fV\n",
           BMS_VOLTAGE_MATCH_PER_MODULE * BMS_NUM_MODULES);
    printf("  Pack resistances: [%.1fmOhm, %.1fmOhm, %.1fmOhm]\n",
           corvus_pack_resistance(temps[0], socs[0]) * 1e3,
           corvus_pack_resistance(temps[1], socs[1]) * 1e3,
           corvus_pack_resistance(temps[2], socs[2]) * 1e3);
    printf("  Thermal mass: %.2f MJ/C, Cooling: %.0f W/C\n",
           BMS_THERMAL_MASS / 1e6, BMS_THERMAL_COOLING_COEFF);
    printf("  Ambient: %.0fC\n\n", BMS_AMBIENT_TEMP);

    double dt = 1.0;
    double t = 0.0;
    double ext_heat[NUM_PACKS] = {0};

    /* Track which packs have been logged as connected */
    int connected_logged[NUM_PACKS] = {0};

    /* ── PHASE 1: Connect to charge (t=0..30s) ── */
    printf("[Phase 1] Connect-to-charge -- lowest SoC first, then simultaneous (Section 7.2.1)\n");
    printf("  Pack SoCs: [%.0f%%, %.0f%%, %.0f%%]\n",
           socs[0]*100, socs[1]*100, socs[2]*100);

    int num_connected = 0;
    int first_connected = 0;
    for (int step = 0; step < 30; step++) {
        if (!first_connected)
            corvus_array_connect_first(&array, true);
        else
            corvus_array_connect_remaining(&array, true);

        corvus_array_step(&array, dt, 0.0, NULL);
        record(t, &array);

        for (int i = 0; i < NUM_PACKS; i++) {
            if (array.controllers[i].mode == BMS_MODE_CONNECTED && !connected_logged[i]) {
                connected_logged[i] = 1;
                first_connected = 1;
                num_connected++;
                printf("  t=%.0fs: Pack %d CONNECTED (SoC=%.1f%%, dV=%.1fV)\n",
                       t, array.controllers[i].pack.pack_id,
                       array.controllers[i].pack.soc * 100.0,
                       fabs(array.controllers[i].pack.pack_voltage - array.bus_voltage));
            }
        }
        t += dt;
    }
    printf("  -> %d/3 packs connected\n\n", num_connected);

    /* ── PHASE 2: Charge at 200A (t=30..330s) ── */
    printf("[Phase 2] Charging at 200A -- Kirchhoff current distribution (Section 7.4)\n");
    double charge_a = 200.0;

    for (int step = 0; step < 300; step++) {
        corvus_array_connect_remaining(&array, true);
        corvus_array_step(&array, dt, charge_a, NULL);
        record(t, &array);

        for (int i = 0; i < NUM_PACKS; i++) {
            if (array.controllers[i].mode == BMS_MODE_CONNECTED && !connected_logged[i]) {
                connected_logged[i] = 1;
                num_connected++;
                printf("  t=%.0fs: Pack %d CONNECTED\n",
                       t, array.controllers[i].pack.pack_id);
            }
        }

        if (step == 10) {
            printf("  t=%.0fs: Kirchhoff distribution: [%.1fA, %.1fA, %.1fA]\n",
                   t,
                   array.controllers[0].pack.current,
                   array.controllers[1].pack.current,
                   array.controllers[2].pack.current);
            printf("    Bus voltage: %.1fV\n", array.bus_voltage);
        }
        t += dt;
    }
    printf("  SoCs: [%.1f%%, %.1f%%, %.1f%%]\n",
           array.controllers[0].pack.soc * 100,
           array.controllers[1].pack.soc * 100,
           array.controllers[2].pack.soc * 100);
    printf("  Temps: [%.1fC, %.1fC, %.1fC]\n\n",
           array.controllers[0].pack.temperature,
           array.controllers[1].pack.temperature,
           array.controllers[2].pack.temperature);

    /* ── PHASE 3: Equalization at zero load (t=330..380s) ── */
    printf("[Phase 3] Zero load -- equalization currents between packs\n");
    for (int step = 0; step < 50; step++) {
        corvus_array_step(&array, dt, 0.0, NULL);
        record(t, &array);

        if (step == 5) {
            printf("  t=%.0fs: Equalization currents: [%.2fA, %.2fA, %.2fA]\n",
                   t,
                   array.controllers[0].pack.current,
                   array.controllers[1].pack.current,
                   array.controllers[2].pack.current);
            printf("    SoCs: [%.2f%%, %.2f%%, %.2f%%]\n",
                   array.controllers[0].pack.soc * 100,
                   array.controllers[1].pack.soc * 100,
                   array.controllers[2].pack.soc * 100);
            printf("    Bus voltage: %.1fV\n", array.bus_voltage);
        }
        t += dt;
    }
    printf("\n");

    /* ── PHASE 4: Overcurrent warning (t=380..440s) ── */
    printf("[Phase 4] Overcurrent warning test (simulated EMS bypass)\n");
    bms_current_limit_t tc_lim = corvus_temp_current_limit(
        array.controllers[0].pack.temperature, BMS_NOMINAL_CAPACITY_AH);
    double oc_threshold = 1.05 * tc_lim.charge + 5.0;
    printf("  Temp charge limit: %.0fA, OC warning threshold: %.0fA\n",
           tc_lim.charge, oc_threshold);

    int oc_warned = 0;
    for (int step = 0; step < 40; step++) {
        corvus_array_step(&array, dt, 100.0, NULL);
        /* Simulate EMS override: force pack 1 current above OC threshold */
        if (step < 25)
            array.controllers[0].pack.current = oc_threshold + 20.0;
        record(t, &array);

        for (int i = 0; i < NUM_PACKS && !oc_warned; i++) {
            if (array.controllers[i].has_warning &&
                strstr(array.controllers[i].warning_message, "OC") != NULL) {
                printf("  t=%.0fs: OC WARNING triggered (after 10s delay)\n", t);
                oc_warned = 1;
            }
        }
        t += dt;
    }

    /* Back to normal */
    for (int step = 0; step < 20; step++) {
        corvus_array_step(&array, dt, 100.0, NULL);
        record(t, &array);
        t += dt;
    }
    if (!oc_warned)
        printf("  OC warning not triggered in 40s (check timer)\n");
    printf("\n");

    /* ── PHASE 5: Cooling system failure on Pack 3 during heavy charging ── */
    /* Realistic maritime incident: fan failure reduces cooling from 800 to 50 W/°C
     * (natural convection only). High current charging + adjacent machinery heat. */
    double reduced_cooling = 50.0;   /* W/°C, natural convection only */
    double adjacent_heat = 50000.0;  /* 50 kW from adjacent machinery */

    printf("[Phase 5] Cooling system failure on Pack 3 -- fan failure during heavy charging\n");
    printf("  Normal cooling: %.0f W/C -> Fan failure: %.0f W/C\n",
           BMS_THERMAL_COOLING_COEFF, reduced_cooling);
    printf("  Adjacent machinery heat: %.0f kW\n", adjacent_heat / 1e3);
    printf("  Warning: %.0fC, Fault: %.0fC, HW Safety: %.0fC\n",
           BMS_SE_OVER_TEMP_WARNING, BMS_SE_OVER_TEMP_FAULT, BMS_HW_SAFETY_OVER_TEMP);

    int warn_logged = 0, fault_logged = 0;

    for (int step = 0; step < 700; step++) {
        double current = array.controllers[2].fault_latched ? 0.0 : 900.0;
        memset(ext_heat, 0, sizeof(ext_heat));

        if (!array.controllers[2].fault_latched) {
            /* Simulate fan failure: compensate built-in cooling to achieve
             * effective cooling of reduced_cooling W/°C */
            double cooling_comp = (BMS_THERMAL_COOLING_COEFF - reduced_cooling) *
                                  (array.controllers[2].pack.temperature - BMS_AMBIENT_TEMP);
            ext_heat[2] = cooling_comp + adjacent_heat;
        }
        /* After fault: fan restored, no external heat (normal cooling resumes) */

        corvus_array_step(&array, dt, current, ext_heat);
        record(t, &array);

        if (array.controllers[2].pack.temperature >= BMS_SE_OVER_TEMP_WARNING && !warn_logged) {
            printf("  t=%.0fs: Pack 3 OT WARNING -- %.1fC\n",
                   t, array.controllers[2].pack.temperature);
            printf("    Charge limit: %.1fA\n", array.controllers[2].charge_current_limit);
            warn_logged = 1;
        }

        if (array.controllers[2].fault_latched && !fault_logged) {
            printf("  t=%.0fs: Pack 3 FAULT -- %s\n",
                   t, array.controllers[2].fault_message);
            printf("    Contactors OPEN, limits ZERO\n");
            fault_logged = 1;
        }

        if (fault_logged)
            break;
        t += dt;
    }

    /* Let the fault state settle */
    for (int step = 0; step < 10; step++) {
        corvus_array_step(&array, dt, 80.0, NULL);
        record(t, &array);
        t += dt;
    }

    printf("  Pack 3 mode: %s, temp: %.1fC\n\n",
           bms_mode_name(array.controllers[2].mode),
           array.controllers[2].pack.temperature);

    /* ── PHASE 6: Temperature hysteresis demo ── */
    printf("[Phase 6] Warning hysteresis -- cooling restored, hold time prevents premature clear\n");
    printf("  Warning hold time: %.0fs\n", BMS_WARNING_HOLD_TIME);

    for (int step = 0; step < 15; step++) {
        corvus_array_step(&array, dt, 80.0, NULL);  /* Normal cooling resumes */
        record(t, &array);
        t += dt;
    }
    printf("  Pack 3 temp after cooling restored: %.1fC\n",
           array.controllers[2].pack.temperature);
    printf("  Pack 3 warning still active: %s\n\n",
           array.controllers[2].has_warning ? "true" : "false");

    /* ── PHASE 7: Fault reset attempt ── */
    printf("[Phase 7] Fault latch -- reset denied, then wait for hold time\n");

    int result = corvus_controller_manual_fault_reset(&array.controllers[2]);
    printf("  Reset attempt @ %.1fC, safe_time=%.0fs: %s\n",
           array.controllers[2].pack.temperature,
           array.controllers[2].time_in_safe_state,
           result ? "OK" : "DENIED");

    /* Cool Pack 3 via normal cooling (fan restored) */
    printf("  Waiting for Pack 3 to cool below fault threshold (normal cooling)...\n");
    for (int step = 0; step < 200; step++) {
        corvus_array_step(&array, dt, 80.0, NULL);
        record(t, &array);
        t += dt;
    }

    printf("  Pack 3 temp: %.1fC, safe_time: %.0fs\n",
           array.controllers[2].pack.temperature,
           array.controllers[2].time_in_safe_state);

    result = corvus_controller_manual_fault_reset(&array.controllers[2]);
    printf("  Reset attempt: %s\n", result ? "SUCCESS" : "DENIED (need more hold time)");

    if (!result) {
        for (int step = 0; step < 120; step++) {
            corvus_array_step(&array, dt, 80.0, NULL);
            record(t, &array);
            t += dt;
        }
        result = corvus_controller_manual_fault_reset(&array.controllers[2]);
        printf("  After more cooling -- safe_time: %.0fs\n",
               array.controllers[2].time_in_safe_state);
        printf("  Reset: %s\n", result ? "SUCCESS" : "DENIED");
    }
    printf("\n");

    /* ── PHASE 8: Reconnect and disconnect ── */
    printf("[Phase 8] Reconnect Pack 3 + disconnect all\n");

    if (array.controllers[2].mode == BMS_MODE_READY)
        corvus_controller_request_connect(&array.controllers[2],
                                          array.bus_voltage, true);

    int reconnected = 0;
    for (int step = 0; step < 30; step++) {
        corvus_array_step(&array, dt, 80.0, NULL);
        record(t, &array);

        if (array.controllers[2].mode == BMS_MODE_CONNECTED && !reconnected) {
            printf("  t=%.0fs: Pack 3 RECONNECTED\n", t);
            reconnected = 1;
        }
        t += dt;
    }
    printf("  Modes: [%s, %s, %s]\n",
           bms_mode_name(array.controllers[0].mode),
           bms_mode_name(array.controllers[1].mode),
           bms_mode_name(array.controllers[2].mode));

    /* Disconnect all */
    corvus_array_disconnect_all(&array);

    for (int step = 0; step < 20; step++) {
        corvus_array_step(&array, dt, 0.0, NULL);
        record(t, &array);
        t += dt;
    }

    printf("  Final modes: [%s, %s, %s]\n",
           bms_mode_name(array.controllers[0].mode),
           bms_mode_name(array.controllers[1].mode),
           bms_mode_name(array.controllers[2].mode));
    printf("  Final SoCs: [%.1f%%, %.1f%%, %.1f%%]\n",
           array.controllers[0].pack.soc * 100,
           array.controllers[1].pack.soc * 100,
           array.controllers[2].pack.soc * 100);
    printf("  Final temps: [%.1fC, %.1fC, %.1fC]\n\n",
           array.controllers[0].pack.temperature,
           array.controllers[1].pack.temperature,
           array.controllers[2].pack.temperature);

    /* Write CSV */
    write_csv("corvus_output.csv");

    printf("\nDone.\n");
    return 0;
}
