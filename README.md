# Corvus Orca ESS Demo v4

> **Disclaimer:** Independent simulation of Orca ESS interface behaviors for
> integration testing and educational purposes. Not affiliated with, endorsed
> by, or derived from Corvus Energy's proprietary software. Interface behaviors
> are implemented from publicly-available integrator documentation for
> interoperability purposes.

A single-file Python implementation of the Corvus Energy Orca ESS Pack ⇄ EMS interface, grounded in publicly-available integrator documentation.

**Reference:** Corvus Energy Orca ESS integrator documentation (1007768 Rev V for traceability)

## What This Is

A runnable demo that implements the core Pack Controller behaviors documented in Corvus Energy Orca ESS integrator documentation, driving them with a physically-grounded equivalent-circuit battery model. Every behavior cites the exact manual section it implements.

## How to Run

```bash
pip install numpy matplotlib
python corvus_demo.py
```

**Outputs:**
- `corvus_output.csv` — time series of all pack states (~865 rows)
- `corvus_plot.png` — 5-panel plot showing the full scenario

## v4 Changes from v3

1. **2D resistance lookup** — R(SoC, T) with bilinear interpolation; baseline 3.3 mΩ/module from manual p.32
2. **Realistic thermal mass** — 1,386,000 J/°C (22 modules × 60 kg × 1050 J/kg/K), up from 5000 J/°C
3. **Forced-air cooling** — 800 W/°C coefficient
4. **24-point NMC 622 OCV curve** from literature
5. **Figure 28/29/30 breakpoints** — temperature, SoC, and SEV current limits from extracted manual figures
6. **Double-step bug fixed** — PackController.step() no longer drives pack physics; only ArrayController does
7. **HW safety independence** — runs in try/except, fires regardless of fault_latched state, appends to fault_message
8. **All current limits clamped to ≥ 0** — prevents negative limit values at extreme SoC
9. **OC discharge warning sign corrected** — `I < -(1.05 × td - 5.0)` per Table 13
10. **OC fault charge-only at sub-zero** — Table 13 only specifies OC fault for T < 0°C AND charging
11. **SEV-based current limit** — Figure 30 breakpoints; final limit = min(temp, soc, sev)
12. **Warning hysteresis** — deadband on clear thresholds (e.g., OV warn at 4.210V, clear at 4.190V) + 10s minimum hold time; no blanket 50% derating
13. **Connect ordering** — first pack by SoC, remaining simultaneously (not one-at-a-time)
14. **Kirchhoff solver improvements** — iteration cap = len(conn), post-solve assertion, equalization currents at zero load
15. **Temperature limit continuity** — linear ramp from 65°C to 70°C (no discontinuity)
16. **Fault reset with hold time** — `manual_fault_reset()` requires 60s below thresholds; SW/HW fault distinction
17. **Ambient 40°C** — realistic engine room temperature
18. **Phase 4 cooling via thermal model** — uses external_heat, not manual temperature override
19. **KCL-correct equalization solver** — iterative clamp-and-remove maintains ΣI_k = 0 constraint
20. **Warning message accumulation** — multiple simultaneous warnings collected and joined, not overwritten
21. **Bus voltage init uses mean** — neutral choice when charge/discharge intent unknown
22. **Temperature floor at -40°C** — prevents sub-arctic absurdity in thermal model
23. **Dead code removed** — duplicate cell_voltage assignment deleted

## What It Demonstrates

### Phase 1: Sequential Pre-charge → Parallel Connection (Section 7.2.1)
First pack (lowest SoC) pre-charges the bus (5s per Table 16). Remaining packs connect simultaneously once first is on-bus and voltage/SoC are in range.

### Phase 2: Charging with Kirchhoff Current Distribution (Section 7.4)
200A charge distributed by impedance across 3 packs. Pack with lowest SoC (highest OCV deficit) draws more current. Current limits from min(temp, SoC, SEV) curves.

### Phase 3: Equalization at Zero Load
With no external current, circulating equalization currents flow between packs with different SoCs. Higher-SoC packs discharge into lower-SoC packs through the bus.

### Phase 4: Overcurrent Warning (Table 13)
Simulated EMS bypass pushes Pack 1 current above `1.05 × temp_limit + 5A`. Warning triggers after 10s delay per Table 13.

### Phase 5: Temperature Ramp → Warning → Fault (Table 13)
External heat on Pack 3:
- **Warning at 60°C** (5s delay) — current limits naturally derate via Figure 28
- **Fault at 65°C** (5s delay) — contactors open, limits zero
- HW safety at 70°C as independent backstop

### Phase 6: Warning Hysteresis
After slight cooling, Pack 3 warning doesn't clear immediately — 10s minimum hold time enforced.

### Phase 7: Fault Reset with Hold Time (Section 6.3.5)
Reset denied when conditions haven't been below threshold for 60s. After sufficient cooling and hold time, operator reset succeeds.

### Phase 8: Reconnection and Disconnect
Pack 3 reconnects to bus, then all packs disconnect cleanly.

## Architecture

| Component | Description |
|-----------|-------------|
| `VirtualPack` | OCV(SoC) + R(SoC,T) equivalent circuit, coulomb counting, first-order thermal (1.39 MJ/°C) |
| `PackController` | 7-mode state machine, pre-charge timing, current limits (temp/SoC/SEV), alarm system with HW safety independence |
| `ArrayController` | Multi-pack orchestration, Kirchhoff bus solver with equalization, connect ordering |
| `run_scenario()` | 8-phase test harness exercising all behaviors |

## Key Parameters

| Parameter | Value | Source |
|-----------|-------|--------|
| R_module (25°C, mid-SoC) | 3.3 mΩ | Corvus manual p.32 |
| R_pack (22 modules) | 72.6 mΩ | Derived |
| Cells per module | 14 | 50V / 3.6V nominal |
| Cells per pack | 308 | 14 × 22 |
| Module capacity | 128 Ah | Section 1.3 |
| Thermal mass | 1,386,000 J/°C | 22 × 60 kg × 1050 J/(kg·K) |
| Cooling coefficient | 800 W/°C | Forced-air estimate |
| Ambient temperature | 40°C | Engine room estimate |
| Pre-charge duration | 5s | Table 16 |
| Fault reset hold time | 60s | Conservative |
| Warning hold time | 10s | — |

## Manual Citations

- **Table 13** — Alarm thresholds (voltage, temperature, overcurrent, delays)
- **Table 15** — Pack operation modes
- **Section 6.2** — Hardware safety system (independent of software)
- **Section 6.3.4** — Warning behavior ("may be reduced depending on nature")
- **Section 6.3.5** — Fault latching and manual reset ("must not be automated")
- **Section 7.2** — Connection logic, voltage matching, pre-charge
- **Section 7.2.1/7.2.2** — Connect-all ordering (charge vs discharge)
- **Section 7.4** — Current limits (Figures 28, 29, 30)
- **Figure 28** — Temperature-based current limit
- **Figure 29** — SoC-based current limit (BOL)
- **Figure 30** — SEV-based current limit

## Known Limitations

1. **HW safety is simulated in software** — Not an independent hardware protection layer. No watchdog timer, contactor welding detection, or feedback verification.

2. **No CAN/Modbus communication timeout modeling** — Real systems detect communication loss and enter safe state.

3. **No ground fault / insulation monitoring** — No IMD simulation.

4. **No pre-charge inrush current modeling** — Timer-only pre-charge; no RC circuit or current waveform.

5. **No cell balancing or per-cell monitoring** — Lumped single-cell model. Real Orca ESS monitors each of 264 cells individually (Section 7.6.1).

6. **No aging, SOH, capacity fade, or calendar degradation** — Uses BOL curves for Figure 29. No self-discharge.

7. **Equalization currents may have small KCL residual** — After per-pack clamping, ΣI_k ≈ 0 but not exactly zero.

8. **Array current limits use min×N** — Per manual Section 7.4 example. This is conservative; real systems may use per-pack limits.

9. **Warning hysteresis deadbands and fault reset hold time are engineering choices** — Not specified in the manual; values chosen for reasonable demo behavior.

10. **Subset of pack modes** — Implements OFF → READY → CONNECTING → CONNECTED → FAULT cycle. POWER_SAVE, NOT_READY, MAINTENANCE, and full OFF transitions not simulated.

11. **Sign convention** — Positive current = charging. Verify against your EMS convention.

12. **Resistance temperature/SoC variation** — Based on published NMC pouch cell literature scaled to the 3.3 mΩ baseline. Medium confidence for off-nominal conditions.

13. **Thermal model** — Single lumped node per pack. Real thermal gradients within modules not captured.

14. **No sensor-based current limit** — Section 7.4.4 (PDM current sensor rating) not implemented.

15. **OC warning simulation** — Phase 4 injects current directly to bypass Kirchhoff solver, simulating an EMS that ignores BMS limits.

16. **SoC convergence not modeled** — Remaining packs connect based on voltage match only; manual §7.2.1 also gates on SoC convergence between connected and ready packs.

17. **Lumped thermal model** — Thermal model is lumped per-pack (no cell-to-cell thermal gradients or module-level thermal stratification).
