# Corvus Orca ESS Demo — Fix Plan

Based on three independent critic reviews (~40 findings total), triaged against the 1007768 Rev V Integrator Manual.

---

## Tier 1 — Fixable Now From Manual Alone

These have clear fixes derivable from the manual text.

### 1.1 Missing Pre-Charge Sequence
- **What's wrong:** `CONNECTING → CONNECTED` is instant. Real packs connect through a pre-charge resistor (5–22 seconds depending on module count).
- **Fix:** Add a timed `CONNECTING` state with configurable pre-charge duration. For 22 modules: 5s minimum (Table 16). First pack pre-charges the bus; subsequent packs connect after bus voltage is established.
- **Ref:** Section 7.2.4, Table 16

### 1.2 Connect-to-Charge Ordering Wrong
- **What's wrong:** Code SoC-sorts all packs and connects them sequentially. Manual says only the *first* pack connects by lowest SoC; remaining packs connect simultaneously once the first is connected and their SoC/voltage is within range.
- **Fix:** Connect lowest-SoC pack first, then attempt all remaining simultaneously (voltage/SoC gated, not SoC-ordered).
- **Ref:** Section 7.2.1 — Manual specifies first-pack-then-simultaneous connection ordering: remaining packs connect simultaneously (not SoC-ordered) once first pack is on-bus and voltage/SoC are in range.

### 1.3 `for_charge` Parameter Ignored
- **What's wrong:** `request_connect(bus_voltage, for_charge=True)` accepts `for_charge` but never uses it.
- **Fix:** Use it to select SoC ordering: lowest-first for charge (7.2.1), highest-first for discharge (7.2.2). Also affects which pack pre-charges first.
- **Ref:** Sections 7.2.1, 7.2.2

### 1.4 Missing Overcurrent Detection (Table 13)
- **What's wrong:** No overcurrent warning or fault checks at all.
- **Fix:** Add two checks:
  - **Overcurrent warning:** `|I| > 1.05 × temp_current_limit + 5A`, 10s delay
  - **Overcurrent fault:** Only when `T < 0°C` and `I > 1.05 × temp_charge_limit + 5A`, 5s delay
- **Ref:** Table 13

### 1.5 Missing SEV-Based Current Limit
- **What's wrong:** Only temperature and SoC limits implemented. The SEV (cell voltage) based limit from Figure 30 is missing entirely.
- **Fix:** Add a voltage-based derating function. From Figure 30: charge limit ramps down as cell voltage approaches OV warning/fault thresholds; discharge limit ramps down as cell voltage approaches UV thresholds. Take `min(temp_limit, soc_limit, sev_limit)` for final limit.
- **Ref:** Section 7.4.3, Figure 30

### 1.6 Warning Derating Is Fabricated
- **What's wrong:** Code applies blanket `0.5×` to current limits on any warning. Manual indicates current limits may be reduced on warning, but does not specify a fixed multiplier.
- **Fix:** Remove the blanket 50% multiplier. Instead, the current limits should naturally reflect the condition that caused the warning (since temp/SoC/SEV limits already handle derating). If a warning is active, the limit *is already reduced* by the parametric derating. The warning is an *indication*, not an additional derating layer.
- **Ref:** Section 6.3.4 — manual states current reduction depends on warning type, not a fixed multiplier.

### 1.7 Automated Fault Reset Contradicts Manual
- **What's wrong:** `reset_faults()` is called programmatically in the demo scenario. Manual requires latched faults with manual operator reset; automated reset is prohibited (Section 6.3.5).
- **Fix:** Keep the method but rename it `manual_fault_reset()` and add prominent comments. In the demo, frame it as "simulating operator action" with a print statement. Never auto-call it in a loop.
- **Ref:** Section 6.3.5

### 1.8 Alarms Checked Against Stale State
- **What's wrong:** In `PackController.step()`, `_check_alarms(dt)` runs *before* `self.pack.step()`, so alarms evaluate the previous timestep's voltage/temperature.
- **Fix:** Reorder: run `self.pack.step()` first, then `_check_alarms()`, then compute current limits.
- **Ref:** Logic bug, not manual-specific

### 1.9 Pack Model Uses Previous Timestep's Current
- **What's wrong:** `PackController.step()` calls `self.pack.step(dt, self.pack.current, ...)` — but `self.pack.current` hasn't been updated yet for this timestep. The actual current is set later in `ArrayController.step()`.
- **Fix:** Restructure the simulation loop:
  1. Compute current limits for all packs
  2. Compute array limits and distribute current
  3. Step all pack physics models with the *actual* current
  4. Check alarms on new state

### 1.10 Warning Clear Logic Asymmetry
- **What's wrong:** OV warning clears when `v < SE_OVER_VOLTAGE_WARNING` (strict `<`), but UV warning clears when `v > SE_UNDER_VOLTAGE_WARNING` (strict `>`). The combined clear check uses `<` for OV and `>` for UV. But the *trigger* uses `>=` for OV and `<=` for UV, creating a one-sample hysteresis on one side but not the other.
- **Fix:** Use consistent comparison: trigger on `>=`/`<=`, clear when strictly past threshold in safe direction. The current approach is close but document it or make symmetric.

### 1.11 OCV Curve: 0% SoC = UV Fault Threshold
- **What's wrong:** `_OCV_BP[0] = 3.00V` exactly equals `SE_UNDER_VOLTAGE_FAULT = 3.000V`. A pack at 0% SoC immediately faults — no safety buffer.
- **Fix:** Set 0% SoC OCV to ~3.10V (above UV fault, below UV warning of 3.2V). This gives a buffer zone. Alternatively, shift the SoC range so 0% maps to a slightly higher voltage.
- **Ref:** Table 13 (UV fault = 3.0V), Section 7.3 — manual warns continued operation below 3.0V SEV risks permanent cell damage.

### 1.12 No `__main__` Guard
- **What's wrong:** `matplotlib.use('Agg')` and plot imports execute on import. There IS a `__main__` guard at the bottom, but matplotlib is configured at module level.
- **Fix:** Move matplotlib import and `use('Agg')` inside `_make_plot()` or behind a lazy import.

### 1.13 Missing `complete_connection` Voltage Recheck
- **What's wrong:** Once a pack enters CONNECTING, `complete_connection()` transitions it to CONNECTED without rechecking that voltage match is still valid (bus voltage may have changed since request_connect).
- **Fix:** Pass current bus_voltage to `complete_connection()` and recheck the delta before closing contactors.
- **Ref:** Section 7.2 — pack controller verifies bus and load voltages are within safe limits before closing contactors.

### 1.14 Wrong Section Citations
- **What's wrong:** Several comments cite wrong section numbers.
- **Fix:** Audit all `Section X.X` citations against the actual manual. Known issues:
  - "Section 2.1" for 22 modules — 2.1 is about pack identification, not module count
  - Various figure numbers may have shifted between revisions

---

## Tier 2 — Needs Research

### 2.1 Internal Resistance Value and Dependencies
- **What's wrong:** `R_INTERNAL_PER_CELL = 0.0008 Ω` (0.8 mΩ) is likely too low and is constant (no SoC or temperature dependence).
- **What we need:** Realistic NMC pouch cell internal resistance at ~128 Ah capacity, and how it varies with temperature (especially below 10°C) and SoC (especially at extremes).
- **Manual hints:** None — manual doesn't expose cell-level electrical parameters.
- **Search queries:**
  - `"NMC pouch cell" "internal resistance" "100-150Ah" mΩ temperature dependence`
  - `lithium ion battery equivalent circuit model resistance vs SoC vs temperature`
  - `Corvus Orca module cell specifications NMC`
- **Realistic range:** Likely 1.0–3.0 mΩ per cell depending on SoC/temp. Consider a lookup table R(SoC, T).

### 2.2 Thermal Mass and Cooling Coefficient
- **What's wrong:** `THERMAL_MASS = 5000 J/°C` is likely 10–20× too low for a ~1000V, 128Ah battery pack. `THERMAL_COOLING_COEFF = 0.5 W/°C` is a guess.
- **What we need:** Pack-level thermal mass (J/°C) and effective cooling rate for an air-cooled Orca pack with 22 modules.
- **Manual hints:** Manual mentions fan mechanical failure warning, 230V fan supply, air cooling. No thermal parameters.
- **Search queries:**
  - `"battery pack" "thermal mass" "specific heat" kJ/°C "100 kWh" lithium ion`
  - `NMC pouch cell specific heat capacity J/(kg·K)`
  - `marine battery pack forced air cooling thermal resistance W/°C`
- **Realistic range:** Thermal mass ~50,000–100,000 J/°C for a full pack. Cooling coefficient 2–10 W/°C for forced air.

### 2.3 OCV Curve Accuracy
- **What's wrong:** Only 9 breakpoints, and the shape may not match Corvus's actual NMC chemistry.
- **What we need:** A higher-resolution OCV curve for large-format NMC pouch cells.
- **Manual hints:** Max recommended SoC is 83% / 4.0V (Section 6.4), which constrains the upper curve.
- **Search queries:**
  - `NMC pouch cell OCV curve "open circuit voltage" vs SoC data points`
  - `lithium NMC 622 811 OCV lookup table`
- **Note:** 20+ breakpoints would be more realistic. The 4.20V at 100% SoC aligns with typical NMC.

### 2.4 Temperature-Based Current Limit Curve (Figure 28)
- **What's wrong:** The piecewise approximation of Figure 28 was done from text description, not the actual figure (which is an image in the PDF).
- **What we need:** Extract the actual curve from Figure 28 in the PDF, or get better breakpoints.
- **Action:** Read the PDF figure visually (or OCR) to get accurate charge/discharge C-rate vs temperature breakpoints. The manual shows separate curves for charge and discharge.
- **Search queries:**
  - Extract Figure 28 from the PDF at `reference/pdfs/925876061-1007768-V-Orca-ESS-Integrator-Manual.pdf`

### 2.5 SoC-Based Current Limit Curve (Figure 29)
- **Same issue as 2.4** — piecewise approximation without seeing the actual figure.
- **Action:** Extract Figure 29 from the PDF.

### 2.6 SEV-Based Current Limit Curve (Figure 30)
- **Needed for Tier 1 fix 1.5** — we need the actual curve shape to implement this.
- **Action:** Extract Figure 30 from the PDF.

### 2.7 Bus Voltage as Kirchhoff Constraint
- **What's wrong:** Bus voltage is averaged across connected packs. In reality, all connected packs share the same bus voltage (Kirchhoff's voltage law), and current distributes based on voltage differences and internal resistances.
- **What we need:** Proper parallel battery modeling: V_bus is a single node, each pack's current = (OCV_pack - V_bus) / R_internal_pack.
- **Search queries:**
  - `parallel battery pack bus voltage Kirchhoff model current distribution`
  - `battery management system parallel pack current sharing simulation`
- **Manual hints:** Section 7.4 example confirms packs can have different per-pack current limits on a shared bus.

### 2.8 Sensor-Based Current Limit
- **What's wrong:** Not implemented. Manual says some PDMs have lower-rated current sensors.
- **What we need:** Typical sensor limit values, or just parameterize it.
- **Fix approach:** Add an optional `sensor_current_limit` parameter per pack, default to `None` (no additional limit). When set, include in `min()` of all limits.
- **Ref:** Section 7.4.4 — this is a supply-chain issue, not a physics parameter. Could be a simple configurable cap.

---

## Tier 3 — Acknowledged Simplifications

Document these in README as known limitations of the demo.

### 3.1 Single Lumped Cell Model
- **Reality:** Each module has 14 cells, each monitored individually. Alarms trigger on min/max cell values.
- **Demo simplification:** All cells assumed identical → single voltage/temperature per pack.
- **Impact:** Can't demonstrate cell imbalance, per-cell monitoring, balancing (Section 7.6.1).
- **Disclaimer:** "Demo uses a lumped single-cell model per pack. Real Orca ESS monitors each of 308 cells individually."

### 3.2 Missing POWER_SAVE, NOT_READY, MAINTENANCE, OFF Transitions
- **Reality:** Full state machine with 7+ modes (Table 15, Section 7.1).
- **Demo simplification:** Only implements OFF → READY → CONNECTING → CONNECTED → FAULT cycle.
- **Impact:** Can't demonstrate power save (low SoC protection), maintenance lockout, not-ready states.
- **Disclaimer:** "Demo implements a subset of pack operation modes. POWER_SAVE, NOT_READY, MAINTENANCE, and full OFF transitions are not simulated."

### 3.3 No FAULT → OFF Transition
- **Reality:** Repeated or hardware faults may require full power-off.
- **Demo simplification:** FAULT can only transition to READY via reset.
- **Disclaimer:** Note alongside 3.2.

### 3.4 Current Sign Convention
- **What's wrong:** Code uses positive = charge, negative = discharge. Industry convention is often positive = discharge.
- **Demo choice:** Either convention is valid as long as consistent. The manual itself doesn't clearly mandate a convention.
- **Disclaimer:** "Demo uses positive current = charging. Verify against your EMS convention."

### 3.5 22 Modules as Default
- **Reality:** Manual supports 7–24 modules per pack (Table 16).
- **Demo choice:** 22 is within range and a common configuration.
- **Disclaimer:** "Default 22 modules; Orca ESS supports 7–24 modules per pack."

### 3.6 No Exception Handling / CSV/Plot Robustness
- **This is a demo script**, not production code. Adding try/except around file I/O and plot generation would be nice but isn't critical to demonstrating BMS behavior.

### 3.7 Phase 4 Thermal: External Cooldown vs Model
- **What's wrong:** Phase 4 manually decrements `packs[2].temperature` while the thermal model also runs, creating conflicting thermal dynamics.
- **Fix approach:** Either disable the thermal model during cooldown phases, or inject cooling via `AMBIENT_TEMP` manipulation. But for a demo, just document it.
- **Disclaimer:** "Phase 4 cooldown uses direct temperature manipulation for demonstration purposes."

---

## Tier 4 — Non-Issues or Debatable

### 4.1 `compute_array_limits` Uses min×N
- **Finding:** Should use sum of individual limits instead of min(per-pack) × N.
- **Manual says:** Array limit uses the minimum per-pack limit multiplied by pack count. Manual example: with one pack at 10A limit and nine at 386A, array limit = 10A × 10 = 100A (Section 7.4).
- **Verdict:** min×N confirmed by manual example. This is correct as implemented.

### 4.2 Current Force-Split Equally
- **Finding:** Current should distribute based on voltage/resistance, not equally.
- **Partial issue:** Yes, in reality current distributes per Kirchhoff. But the *demo's job* is to show BMS behavior (limits, alarms, state transitions), not to be a perfect electrical simulator. Equal split is a reasonable simplification *if documented* (see Tier 2 item 2.7 for the full fix).
- **Verdict:** Move to Tier 3 if not doing the Kirchhoff fix.

### 4.3 matplotlib Side Effect on Import
- **Already has `__main__` guard** — `run_scenario()` only executes under `if __name__ == '__main__'`. The `matplotlib.use('Agg')` at module level is standard practice for headless environments. Minor issue at best.

---

## Priority Order for Implementation

1. **Fix simulation loop ordering** (1.8, 1.9) — correctness foundation
2. **Add overcurrent detection** (1.4) — missing safety feature
3. **Add SEV-based current limit** (1.5, needs 2.6) — extract Figure 30 first
4. **Fix connect-to-charge ordering** (1.2, 1.3) — manual compliance
5. **Add pre-charge sequence** (1.1) — manual compliance
6. **Remove fabricated warning derating** (1.6) — correctness
7. **Fix OCV 0% SoC buffer** (1.11) — prevents spurious faults
8. **Fix voltage recheck** (1.13) — safety
9. **Rename/document fault reset** (1.7) — manual compliance
10. **Extract PDF figures** (2.4, 2.5, 2.6) — improves all current limit accuracy
11. **Kirchhoff bus model** (2.7) — impressive to engineers but complex
12. **Better thermal parameters** (2.2) — realism
13. **Better internal resistance** (2.1) — realism
14. **Add disclaimers** (all Tier 3) — documentation pass
