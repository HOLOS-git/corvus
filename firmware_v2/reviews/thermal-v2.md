# Thermal Engineer Delta Review — V2 Firmware

**Reviewer**: Dr. Priya Nair — Battery Thermal Management Specialist  
**Date**: 2026-03-03  
**Scope**: Delta assessment of `firmware_street_smart/` against V1 findings  
**Verdict change**: INADEQUATE → **CONDITIONALLY ADEQUATE** (with caveats)

---

## 1. Executive Summary

The V2 rewrite addresses the most dangerous gap I identified: the firmware now has a thermal brain, not just thermal reflexes. The addition of `bms_thermal.c` (dT/dt detection), `bms_safety_io.c` (gas/fire/ventilation integration), and systematic sensor fault tracking transforms this from a system that could only react to absolute thresholds into one that can detect the *onset* of thermal events. My top three CRITICAL recommendations (R1, R2, R3) are substantively addressed. The risk profile has shifted from "unacceptable for crewed vessel" to "defensible with testing validation."

That said, several V1 concerns remain unresolved, and the implementation introduces new questions.

---

## 2. V1 Findings — Disposition

### ✅ RESOLVED

| V1 Finding | V2 Implementation | Assessment |
|---|---|---|
| **R1: dT/dt detection (CRITICAL)** | `bms_thermal.c` — 30-sample moving window at 1 Hz, 1°C/min threshold, 30s sustain, load-correlation filter | **Well implemented.** Matches my specification almost exactly. The load-correlation heuristic (50A threshold) is crude but functional — see §3 for concerns. |
| **R3: Fire system integration (CRITICAL)** | `bms_safety_io.c` — GPIO for fire detect, suppression input, relay output. dT/dt alarm routed to fire panel. Post-fire manual-only reset in `bms_protection_can_reset()`. | **Substantially addressed.** Bidirectional fire panel communication exists. Post-incident interlock is correct — fire/suppression faults cannot be CAN-reset. |
| **R5: Per-module thermal anomaly detection (HIGH)** | `bms_monitor_aggregate()` computes per-module average temps, flags inter-module delta > 20°C | **Addressed.** The 20°C threshold (`BMS_INTER_MODULE_TEMP_DELTA_DC = 200`) is too generous for my taste — I recommended 5°C — but the mechanism exists and the threshold is a named constant, easily tightened. |
| **R6: Gas detection interface (HIGH)** | `bms_safety_io.c` — GPIO for low/high gas alarms, high alarm → emergency shutdown within 2s, ventilation command output | **Well implemented.** Two-tier response (warning/shutdown) is correct. Emergency ventilation on gas detect is exactly right. |
| **R7: Post-incident hold time (HIGH)** | `bms_protection_can_reset()` — fire/suppression faults require manual intervention, not CAN reset. Rate-limited to 3 resets/hour. | **Excellent.** This closes the 60s auto-reset vulnerability completely. |
| **R10: Sensor health monitoring (MEDIUM)** | `bms_monitor.c` — sentinel detection, plausibility bounds (-40 to 120°C), exact-zero suspicious, adjacent sensor cross-check (>20°C delta), 3 consecutive bad → fault latch | **Comprehensive.** The cross-check logic is particularly good — a failed NTC reading 25°C while neighbors read 60°C will now be caught. |
| **Firmware-simulation gap** | V2 firmware has its own thermal logic independent of the Python simulation | **Resolved** — the firmware is no longer thermally blind. |

### ⚠️ PARTIALLY RESOLVED

| V1 Finding | V2 Status | Gap |
|---|---|---|
| **R2: Cooling system health monitoring (CRITICAL)** | No fan tachometer input, no airflow sensor, no thermal resistance estimator in V2. | **Still missing.** The firmware can detect *consequences* of cooling failure (via dT/dt) but cannot detect the *cause* (fan failure). Detection is delayed by the thermal mass of the pack — could be 5–10 minutes between fan failure and measurable dT/dt. For a forced-air system in a marine environment where fan corrosion is the primary failure mode, this remains a significant gap. |
| **R4: Validate 800 W/°C cooling coefficient** | Not applicable to firmware — this is a simulation/testing item. | **Unchanged.** Still requires physical testing. |
| **R9: Multi-zone thermal model (MEDIUM)** | No predictive thermal model in firmware. | **Accepted.** dT/dt detection provides the most critical capability. A full thermal model would be nice but is not essential given that dT/dt + gas detection + fire integration are now present. |

### ❌ NOT ADDRESSED

| V1 Finding | Status |
|---|---|
| **R8: OCV temperature correction** | SoC estimation still isothermal. Low priority relative to safety items — acceptable deferral. |
| **R11: Thermal design basis document** | Documentation item, not firmware. Still needed before deployment. |
| **§9 Unknown unknowns (1–10)** | All ten items remain unresolvable by code review. Physical barrier configuration, cell format, cooling duct design, vibration effects — all require hardware testing. |

---

## 3. dT/dt Implementation — Detailed Critique

This is the centerpiece thermal improvement, so it deserves scrutiny.

### What's Right

1. **Window size and rate**: 30 samples at 1 Hz = 30-second window. This is appropriate — long enough to filter sensor noise (NTC on battery surface: ±0.5°C typical), short enough to detect thermal runaway onset (which produces dT/dt of 1–10°C/min at the sensor).

2. **Threshold**: 1°C/min (10 deci-°C/min) is correct per Sandia data. Below this, normal operational heating during load transients. Above this without load correlation, something anomalous is happening.

3. **Sustain timer**: 30 seconds of sustained anomalous dT/dt before alarm. This prevents false alarms from brief sensor disturbances while ensuring detection within the critical window (self-heating to runaway is 5–15 minutes for NMC 622).

4. **Fault latching**: dT/dt alarm sets `fault_latched = true` and a distinct fault flag. This is correct — a dT/dt alarm should be treated as seriously as an absolute OT fault.

5. **CAN reporting**: Dedicated CAN ID `0x151` for dT/dt alarm, plus routing to fire panel relay. Good — the vessel's fire safety system gets notified.

6. **Faulted sensor skip**: dT/dt computation correctly skips sensors marked faulted by `bms_monitor.c`. Prevents a failed sensor's last-valid-reading (which is static) from masking a real dT/dt alarm.

### What Concerns Me

**3a. Load correlation heuristic is fragile.**

The current logic: if current has increased by >50A since baseline, the dT/dt is "explained" by load and the alarm timer *decays*. The baseline is updated *every cycle* to the current absolute current.

Problem: baseline updates unconditionally at the end of every `bms_thermal_run()` call. If current ramps up slowly (10A/sec over 50 seconds = 500A increase), the baseline tracks it, and *no single 1-second delta exceeds 50A*. Meanwhile, the cells heat from I²R normally AND could also be entering self-heating — and the load correlation filter could mask it.

**Recommendation**: Baseline should be a slow-moving average (e.g., 60-second window), not instantaneous. Or better: compute *expected* dT/dt from measured current using the known I²R relationship and compare to *measured* dT/dt. The anomaly is the residual, not the absolute rate.

**Confidence**: HIGH — this is a known weakness of load-correlation filters in thermal runaway detection literature.

**3b. Single-point dT/dt vs. spatial dT/dt.**

The algorithm computes dT/dt independently per sensor and alarms if *any one* sensor exceeds the threshold. This is correct for detecting a localized cell-level event, but misses a scenario: gradual pack-wide heating from ambient temperature increase or cooling degradation.

If the entire pack heats at 0.8°C/min (below the 1°C/min threshold) due to failed cooling, no dT/dt alarm fires. The absolute OT threshold at 65°C eventually catches it, but only after the hottest cell (which the sensors may not be measuring) is already at 75–80°C due to the 10–20°C hotspot delta I documented in V1 §2.

**Recommendation**: Add a *pack-average* dT/dt check with a lower threshold (e.g., 0.5°C/min). If the entire pack is heating uniformly, something external has changed (cooling failure, ambient rise). This bridges the gap left by the missing R2 (cooling health monitoring).

**Confidence**: MEDIUM-HIGH — the severity depends on the actual cooling margin, which requires R4 testing.

**3c. dT/dt computation integer arithmetic edge case.**

```c
int16_t dtdt = (int16_t)((int32_t)(t_now - t_old) * 2);
```

`t_now - t_old` is `int16_t` subtraction. If `t_now = 30000` (300.0°C — during thermal runaway) and `t_old = -100` (-10.0°C — failed sensor reading that slipped through), the difference is 30100 × 2 = 60200, which overflows `int16_t` (max 32767). The cast to `int32_t` happens *before* the multiply, so the multiply is safe — but the result is truncated back to `int16_t`. Result: 60200 wraps to a negative value. The alarm *doesn't fire* during the most critical moment.

**Recommendation**: Keep the result as `int32_t` and clamp to `INT16_MAX` before storing. Or — since the sensor fault logic should prevent readings of 300°C from reaching this code — add a defensive check that the intermediate result fits in `int16_t`.

**Confidence**: HIGH for the arithmetic analysis. LOW for real-world impact (sensor fault logic should prevent this input). But defense-in-depth demands fixing it.

---

## 4. Safety I/O — Thermal Implications

### Gas Detection (P1-03)

The two-tier gas response is thermally sound:

- **Low alarm** → warning + increase ventilation. Correct. Low-level off-gassing may indicate early cell venting without full thermal runaway. Ventilation dilutes flammable gases below LEL.
- **High alarm** → emergency shutdown within 2s + fault latch. Correct. High gas concentration means cell venting is active and thermal runaway may be imminent or in progress.

One thermal observation: the gas alarm triggers `hal_gpio_write(GPIO_VENT_CMD, true)` to command ventilation. **This is correct for pre-fire gas management but potentially dangerous if fire is already present.** Increased ventilation feeds oxygen to a fire. The firmware should check `fire_detected` before commanding ventilation increase. Currently, a gas high alarm could command ventilation increase simultaneously with fire detection.

**Recommendation**: Add interlock: if `fire_detected`, suppress the ventilation increase command from gas alarm logic.

### Fire/Suppression (P1-05)

The dT/dt alarm is routed to the fire relay output — this is exactly what I asked for. The fire panel gets early warning from the thermal rate-of-rise *before* absolute temperature thresholds are breached. This could provide 5–15 minutes of advance warning for crew evacuation and suppression preparation.

The post-fire interlock is correct: `bms_protection_can_reset()` refuses CAN-initiated reset if `fire_detected` or `fire_suppression` flags are set. Manual intervention required. This prevents automated systems from re-energizing a pack that has experienced a thermal event.

### Ventilation (P1-04)

Ventilation failure detection with configurable restore delay (30s) is appropriate. The `bms_safety_io_inhibit_close()` function prevents contactor closure during ventilation failure — this is important because closing contactors into a pack with no ventilation means any subsequent fault generates unmanaged flammable gases.

---

## 5. Sensor Fault Detection — Thermal Assessment

The V2 sensor fault system (`bms_monitor.c`) substantially improves the thermal monitoring reliability I was concerned about in V1 §7.

**What's good:**
- Failed NTC detection (sentinel, range check, exact-zero) prevents the firmware from trusting garbage readings
- Cross-check between adjacent sensors within a module catches single-sensor failures
- Faulted sensors are excluded from aggregation and dT/dt computation
- Fault latches — a sensor failure is treated as a safety event, not a transient glitch

**Remaining concern**: The cross-check threshold is 20°C between adjacent sensors in the same module. For normal operation this is reasonable (intra-module gradients are typically 2–5°C). But during a genuine thermal event, a 20°C gradient between adjacent sensors is *expected* and *real*. The cross-check should only flag when temperatures are in the normal operating range — if one sensor reads 65°C and another reads 45°C, that's a real gradient during a thermal event, not a sensor failure.

**Recommendation**: Suppress cross-check warnings when either sensor reads above the OT warning threshold (60°C). At that point, large gradients are physically plausible.

---

## 6. Revised Risk Assessment

### V1 Risk Profile
- Thermal runaway detection: **NONE** (reactive thresholds only)
- Early warning capability: **NONE**
- Fire system integration: **NONE**
- Cooling failure detection: **NONE**
- Sensor reliability: **POOR** (no fault detection)
- Overall thermal safety: **INADEQUATE**

### V2 Risk Profile
- Thermal runaway detection: **GOOD** (dT/dt at 1°C/min with 30s sustain)
- Early warning capability: **GOOD** (dT/dt + gas detection = 5–15 min advance warning)
- Fire system integration: **GOOD** (bidirectional with fire panel, post-incident interlock)
- Cooling failure detection: **MARGINAL** (detected indirectly via dT/dt, no direct fan monitoring)
- Sensor reliability: **GOOD** (comprehensive fault detection and exclusion)
- Overall thermal safety: **CONDITIONALLY ADEQUATE**

### Residual Risks (Ranked)

1. **Cooling failure detection delay** (HIGH) — Without fan tachometer or thermal resistance estimation, cooling failure is detected only after enough heat accumulates to trigger dT/dt. For a pack with ~1.27 MJ/°C thermal mass, this delay could be 5–10 minutes. In a worst case (high ambient + high load), this delay could consume the entire margin between normal operation and self-heating onset.

2. **Pack-wide slow heating undetected** (MEDIUM-HIGH) — Per §3b, uniform heating below the per-sensor dT/dt threshold is invisible until absolute thresholds trigger. The 800 W/°C cooling coefficient assumption (V1 §4) has not been validated and likely overestimates real marine cooling capacity.

3. **Hotspot-to-sensor delta unchanged** (MEDIUM) — The fundamental physics hasn't changed: 3 NTCs per 14 cells means maximum hotspot-to-sensor delta of 15°C at high rate, 28°C+ for degraded cells. dT/dt detection mitigates this (rate-of-rise is detectable even with spatial offset) but doesn't eliminate it.

4. **No predictive thermal trajectory** (MEDIUM) — The firmware still cannot answer "at current conditions, when will we reach OT?" It can only detect that heating is anomalous. For operator decision support (reduce load vs. emergency disconnect), a predictive capability would be valuable.

5. **Aging effects unmodeled** (LOW-MEDIUM) — Impedance growth with cycling creates progressive hotspots. After 2–3 years, thermal gradients could double. The per-module anomaly detection (inter-module delta check) partially mitigates this by flagging modules that diverge from neighbors.

---

## 7. Recommendations — V2 Specific

### HIGH Priority

**T-V2-1: Fix dT/dt baseline tracking** (§3a)  
Use slow-moving average for load correlation baseline, not instantaneous current. Current implementation allows slow ramp-up to mask self-heating.

**T-V2-2: Add pack-average dT/dt check** (§3b)  
Lower threshold (0.5°C/min) on the mean of all non-faulted sensors. Detects uniform heating from cooling failure or ambient rise.

**T-V2-3: Add fan tachometer GPIO** (V1 R2, still missing)  
Most BLDC fans provide tach output. One GPIO per fan, interrupt-driven. Detect stall within 2 seconds instead of 5–10 minutes via dT/dt.

### MEDIUM Priority

**T-V2-4: Clamp dT/dt intermediate result** (§3c)  
Prevent `int16_t` overflow in edge case. Defense-in-depth.

**T-V2-5: Suppress cross-check during thermal events** (§5)  
When either sensor exceeds OT warning, large inter-sensor deltas are physically expected.

**T-V2-6: Interlock ventilation command with fire detection** (§4)  
Gas alarm ventilation command should be suppressed if fire is already detected.

**T-V2-7: Tighten inter-module delta threshold**  
Change `BMS_INTER_MODULE_TEMP_DELTA_DC` from 200 (20°C) to 50 (5°C) as originally recommended. 20°C inter-module delta is a screaming anomaly, not a warning.

---

## 8. Confidence Statement

| Assessment | Confidence | Change from V1 |
|---|---|---|
| dT/dt implementation is functionally correct | **HIGH** | NEW — addresses V1 R1 |
| Load correlation heuristic has exploitable weakness | **HIGH** | NEW concern |
| Fire system integration is adequate for marine deployment | **HIGH** | Addresses V1 R3 |
| Cooling failure detection remains a significant gap | **HIGH** | Downgraded from CRITICAL to HIGH due to indirect detection via dT/dt |
| Sensor fault system is comprehensive | **HIGH** | Addresses V1 §7 |
| Overall thermal safety is conditionally adequate | **MEDIUM-HIGH** | Upgraded from INADEQUATE — conditional on T-V2-1 through T-V2-3 |
| Physical testing requirements unchanged | **HIGH** | V1 §9 items still require hardware validation |

---

## 9. Conditional Approval Criteria

I would sign off on this firmware for marine deployment if:

1. **T-V2-1** (baseline tracking fix) is implemented — prevents the most probable false-negative scenario
2. **T-V2-3** (fan tachometer) is added — closes the last CRITICAL gap from V1
3. **R4** (cooling coefficient validation) produces a measured value ≥ 400 W/°C — below this, the thermal margin under worst-case marine conditions is insufficient regardless of firmware quality
4. UL 9540A module-level testing confirms propagation barriers exist and are effective
5. Gas detectors are physically installed and wired to the GPIO inputs defined in `bms_safety_io.c`

Items 3–5 are hardware/testing requirements, not firmware issues. The firmware is now *ready* for a system that has these physical safeguards. That's a meaningful change from V1, where the firmware itself was the weakest link.

---

*Dr. Priya Nair*  
*Battery Thermal Management Specialist*  
*Delta review completed: 2026-03-03*
