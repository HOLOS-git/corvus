# DELTA REVIEW: Insurance Adjuster (Catherine) — V2

*Reviewer: Catherine Lindqvist — Senior Marine H&M Underwriter, 20 years Nordic P&I*
*Subject: Corvus Orca ESS — BMS Firmware "Street Smart Edition" Rewrite*
*Date: 2026-03-03*
*Reference: V1 Review (FULL_REVIEW.md), original firmware assessment*
*Classification: UNDERWRITING REASSESSMENT — CONFIDENTIAL*

---

## 1. Revised Underwriting Decision

**Decision: CONDITIONAL ACCEPTANCE with reduced premium loading and narrowed exclusions.**

This is a materially different risk profile from V1. The rewrite addresses my four non-negotiable prerequisites — three fully, one partially. I've never seen a firmware revision respond this directly to an underwriting review. Someone was listening.

### Revised Premium Loading: +18–30% over baseline H&M rate

*Down from +45–75% in V1 — a 50–60% reduction in risk loading.*

| Risk Factor | V1 Loading | V2 Loading | Change | Rationale |
|---|---|---|---|---|
| Gas detection interface | +15–20% | +0–2% | **−18%** | P1-03: Full gas detection with GPIO + ADC + 2-level alarm + 2s shutdown interlock. This was my #1 recommendation. Done. |
| Thermal runaway propagation data | +15–25% | +12–20% | **−5%** | Unchanged — still need UL 9540A data. dT/dt helps detection but doesn't change propagation physics. |
| CAN bus authentication | +5–10% | +3–5% | **−5%** | CC-01/P2-01: Sequence counter implemented, CMAC stubbed with config flag. Architecture is there; crypto is deferred. Partial credit. |
| Passive balancing only | +3–5% | +3–5% | **0%** | Unchanged. Still passive. Acceptable for this chemistry/topology. |
| SoH monitoring | +5–10% | +3–7% | **−3%** | dT/dt trending provides indirect degradation signal. Not impedance-based but better than blind. |
| NVM fault log | +2–5% | +1–3% | **−2%** | Expanded fault types (21 types vs 6), IMD trend logging, reset event tracking. Still 64 entries with uptime timestamps. Still inadequate for root cause — but less bad. |
| **NEW: dT/dt detection** | N/A | **−3%** | **Credit** | P0-04: Per-sensor 30-sample moving average, 1°C/min threshold, load-correlation check. This buys 5–15 minutes of warning. Significant. |
| **NEW: IMD monitoring** | N/A | **−2%** | **Credit** | P1-06: IEC 61557-8 compliant, 100kΩ/V threshold, trend logging to NVM. Ground fault detection was a total gap. |
| **NEW: Ventilation monitoring** | N/A | **−1%** | **Credit** | P1-04: Vent status input + failure interlock + contactor close inhibit. |
| **NEW: Fire detection/suppression** | N/A | **−1%** | **Credit** | P1-05: Fire detect + suppression input + manual-only post-fire reset. Correct behavior. |

### Revised Exclusions

**Exclusions that can be DROPPED:**

1. ~~**Thermal events arising from cooling system failure**~~ → **DROPPED with conditions.** The firmware now has dT/dt monitoring (P0-04) that detects anomalous heating independent of absolute temperature. Combined with gas detection (P1-03), the BMS has two independent pathways to detect cooling-related thermal events before they become catastrophic. *Condition: vessel must demonstrate that gas sensors and dT/dt thresholds are included in commissioning verification.*

2. ~~**Losses attributable to CAN bus compromise or spoofing**~~ → **MODIFIED to: "Losses attributable to CAN bus compromise where the BMS_CAN_AUTH_ENABLED flag is set to 0 (disabled)."** The architecture for authentication exists. The sequence counter provides replay protection. When CMAC is implemented and enabled, this exclusion can be fully dropped. Until then, I'm narrowing it rather than removing it.

**Exclusions that REMAIN:**

3. **Consequential losses from SoC estimation error exceeding ±10%** — REMAINS. No independent SoH/SoC validation was added. Coulomb counting + OCV reset is unchanged from V1. The manufacturer will still invoke "operated outside specifications."

4. **Progressive damage from undetected cell degradation beyond Year 3** — MODIFIED to **"beyond Year 5."** The dT/dt monitoring provides an indirect degradation signal (degrading cells show anomalous heating patterns). IMD trend logging catches insulation degradation. I'll extend the coverage window but not eliminate it. Impedance-based SoH would remove this entirely.

**NEW exclusion added:**

5. **Losses during the BMS_CAN_AUTH_ENABLED=0 configuration where the proximate cause is a CAN frame injection attack.** This is a specific, narrow exclusion tied to a documented configuration choice. When authentication is enabled, it drops.

### Revised Survey Conditions

- Annual thermographic survey — **RETAINED** but reduced scope (dT/dt provides continuous monitoring between surveys)
- Semi-annual thermistor verification — **RETAINED** (P0-01 sensor fault detection supplements but doesn't replace physical verification)
- ADC calibration verification — **RETAINED** (no ADC self-check was added)
- Fire suppression test — **RETAINED** (outside firmware scope)
- **NEW: Verify BQ76952 HW protection configuration** — P1-01 added autonomous protection, but the read-back verify only runs at init. Annual verification that ENABLE_PROT_A/B/C registers still hold correct values.
- **NEW: Verify BMS_CAN_AUTH_ENABLED status** — Document whether authentication is active.

**Confidence: HIGH** — The loading reduction is conservative. A more aggressive underwriter might go to +12–20%.

---

## 2. Revised Total Loss Scenario Analysis

### Revised Detection Gap: 30–120s → 10–45s

The critical detection gap I identified in V1 — between "cell begins to fail" and "BMS detects anything" — has been significantly narrowed:

```
V1 Timeline:                          V2 Timeline:
T+0s    Cell short (silent)           T+0s    Cell short (silent)
T+10s   Local heating begins          T+10s   Local heating begins
T+30s   Off-gas (NO DETECTION)        T+30s   Off-gas → GAS LOW alarm (P1-03)
T+60s   Electrolyte vent (NO DET.)    T+35s   dT/dt rising → correlation check
T+90s   Thermal runaway onset         T+40s   GAS HIGH → SHUTDOWN within 2s
T+120s  MAYBE SE OT detects           T+42s   Contactors open + ventilation cmd
                                      T+55s   dT/dt sustain → alarm + latch
```

**The 30–120s blind window is now 10–45s.** Gas detection is the primary improvement (30–90s earlier detection). dT/dt is secondary but provides a second independent channel.

### Revised MCL Probability

The fundamental MCL hasn't changed — a cell-to-cell propagation event can still lead to vessel loss. But the *probability* of reaching that state is materially reduced:

- **P(detection before propagation)**: V1 ≈ 40–60% → V2 ≈ 75–90%
- **P(successful isolation given detection)**: Unchanged at ≈ 85–95% (contactor response)
- **P(fire given isolation failure)**: Unchanged (thermal physics don't care about firmware)
- **P(vessel loss given fire)**: Unchanged (fire suppression is external)

**Net effect**: Expected loss frequency reduced by approximately 40–60%. This is the actuarial basis for the premium reduction.

### Key Residual Risk

The fire detection/suppression interface (P1-05) now gives the BMS awareness of what's happening AFTER thermal runaway begins. The manual-only post-fire reset (`BMS_FIRE_INTERLOCK_MANUAL = true`) is correct — I've seen two incidents where automatic restart after thermal event caused secondary ignition from residual heat in damaged cells.

**Confidence: HIGH** on the detection improvement. **MEDIUM** on probability estimates (still no propagation test data).

---

## 3. Revised Warranty Gap Analysis

### Gaps CLOSED by V2:

| V1 Gap | V2 Status | Impact |
|---|---|---|
| "Thermal events caused by external factors" (cooling failure) | dT/dt + gas detection provide evidence of BMS awareness and response | Manufacturer can no longer claim BMS was "blind." Response timeline is logged. **Gap substantially closed.** |
| "BMS should have detected" (contactor weld) | Weld detection window extended 200ms → 500ms, current logged at time of opening | Better evidence for claims investigation. **Gap narrowed.** |
| IMD/ground fault — nobody's responsibility | P1-06: BMS monitors insulation, logs resistance trend, shuts down on alarm | Clear BMS responsibility boundary established. **Gap closed.** |

### Gaps REMAINING:

| V1 Gap | V2 Status | Why It Persists |
|---|---|---|
| SoC drift → abuse claim | Unchanged | No independent SoC validation. Manufacturer will still claim "operated outside parameters." |
| CAN spoofing → "valid heartbeats" | Partially addressed (seq counter, no CMAC) | Until crypto is enabled, the liability boundary is fuzzy. |
| Sensor placement (3 per 14 cells) | dT/dt helps but doesn't change physics | Adjacent sensor cross-check (P0-01) is good but doesn't add sensors. |

### NEW Warranty Strength

The sensor fault detection system (P0-01) creates a **new liability argument I can use in subrogation**:

If a sensor is faulted (corroded, failed open/short), the firmware now:
1. Detects it (sentinel value, plausibility bounds, 3-consecutive-scan threshold)
2. Flags it (`sensor_fault = true`, `fault_latched = true`)
3. Logs it to NVM
4. Excludes it from aggregation

This means if a manufacturer claims "the BMS should have detected the overtemperature," I can show the BMS *did* detect the sensor failure and *did* latch a protective fault. The sensor was the manufacturer/integrator's component. **This shifts liability toward the component supplier.**

**Confidence: HIGH** — This is a significant improvement in claims defensibility.

---

## 4. Revised Claims Investigation Readiness

### Verdict: IMPROVED but still INADEQUATE for full root cause determination

#### What's Better

1. **21 fault types vs 6**: The NVM now logs gas events, fire events, IMD alarms, dT/dt alarms, plausibility failures, reset events, IWDG resets, and HW protection trips. Each tells a story.

2. **IMD resistance trending**: Logged every 60 seconds to NVM (`NVM_FAULT_IMD_TREND`). Over weeks/months, this builds a degradation picture. I can see insulation declining before it fails. This is genuinely useful for claims investigation.

3. **Reset tracking**: Every fault reset is logged (P2-05: `NVM_FAULT_RESET`). If someone repeatedly resets faults to keep operating, the NVM records it. "Your operator reset the same OT fault 5 times in 2 hours" is powerful evidence in arbitration.

4. **IWDG reset logging**: If the MCU hung and watchdog fired, we know about it. Frequency of IWDG resets indicates firmware stability issues.

#### What's Still Missing

1. **Still 64 entries**: A cascade event still wraps the buffer. IMD trend logging at 60s intervals fills 64 slots in ~64 minutes. If an IMD trend fills the buffer, the next thermal event overwrites old data. The log needs partitioning or expansion.

2. **Still uptime_ms timestamps**: No RTC. No absolute time. I still can't correlate BMS events with vessel navigation log, engine room CCTV, or crew witness statements. In arbitration, the other side's lawyer will ask "what time did this happen?" and I'll say "we know it was 47,293 milliseconds after some unknown boot event."

3. **Still no continuous data logging**: The firmware logs *events* (fault transitions) but not *state* (cell voltages, temperatures, currents over time). The 10–30 minutes of pre-fault trending I need for root cause is still not captured. dT/dt internally computes rate-of-rise but doesn't log the raw temperature history to NVM.

4. **Still no tamper evidence**: No CRC chain, no signing. NVM data can be challenged.

#### Net Assessment

V1: "Root cause determination likely impossible from BMS data alone."
V2: "Root cause determination possible for some failure modes (sensor degradation, insulation failure, repeated-reset abuse). Still impossible for sudden cell failure events."

**Confidence: HIGH** — The improvement is real but the fundamental architecture of 64-event ring buffer with relative timestamps is unchanged.

---

## 5. Revised Classification Compliance

### Gap 1: Independent Protection System — **SUBSTANTIALLY ADDRESSED**

P1-01 is the most important change in this rewrite. The BQ76952's autonomous hardware protection is now explicitly configured at init:

- `ENABLE_PROT_A`: SC + OC + OV + UV — BQ76952 trips its own FETs
- `ENABLE_PROT_B`: OT charge + OT discharge + OT FET
- Thresholds written to data memory (OV=4300mV, UV=2700mV, OT=70°C)
- **Read-back verified** (P2-07) — every config write is confirmed

This means if the STM32F4 hangs completely:
- The BQ76952 ASIC still monitors all cell voltages at silicon level
- The BQ76952 ASIC still monitors temperatures via TS1/TS2/TS3
- The BQ76952 will independently trip FETs on OV/UV/OT/SC

Additionally, the IWDG (P1-02, ≤100ms timeout) will reset the MCU if the protection loop stalls. On restart, `bms_contactor_init()` opens all contactors.

**Assessment**: This is now a credible two-layer protection system. The BQ76952 ASIC constitutes a genuinely independent hardware layer. A class surveyor should accept this with the documentation I see in the code comments. I would note that the ASIC FET control is for the internal BQ76952 FETs, not necessarily the main pack contactors — the independence argument is strongest for cell-level protection and relies on the IWDG for pack-level contactor response.

**Residual concern**: The read-back verify only runs at `bq76952_init()`. If a BQ76952 register corrupts during operation (SEU, power glitch), there's no periodic re-verification. This is an edge case but it exists.

### Gap 2: Gas Detection — **CLOSED**

P1-03: Two-level gas alarm (low → warning + ventilation boost, high → shutdown within 2s). GPIO inputs for both digital alarm levels plus ADC for analog concentration. CAN message (0x150) broadcasts status. This is textbook compliant.

### Gap 3: Ground Fault / Insulation Monitoring — **CLOSED**

P1-06: IMD alarm GPIO + analog resistance reading + IEC 61557-8 threshold + automatic shutdown + trend logging. Exactly what I asked for.

### Gap 4: Redundant Communication — **UNCHANGED**

Still single CAN bus. Still a single point of failure. The hardware filter (P2-08: `hal_can_set_filter`) reduces noise but doesn't add redundancy.

### Gap 5: Ventilation Monitoring — **CLOSED**

P1-04: Vent status input, failure alarm, contactor close inhibit, configurable restore delay (30s). Correct behavior.

### Gap 6: Software Verification — **IMPROVED**

The "SIMULATION DISCLAIMER" is gone. Code is structured, documented with reviewer finding references, and includes a comprehensive mock HAL for testing. Still no formal safety case, FMEA, or test coverage report — but the architecture is clearly production-oriented.

**Confidence: HIGH** on gaps 1–3, 5. **MEDIUM** on gap 6 (documentation exists in code comments but no formal safety case).

---

## 6. Revised Recommendations

### Priority 1: Non-negotiable for coverage

1. ~~**Install gas detection**~~ → **DONE (P1-03)**. 
2. **Provide UL 9540A propagation test data** → **STILL REQUIRED.** This is my remaining #1 risk driver. The firmware improvements reduce detection time but the physics of cell-to-cell propagation haven't changed. I'm still pricing blind on propagation probability.
3. **Install continuous black-box data logging** → **STILL REQUIRED.** The 64-entry NVM ring buffer is better-populated but still fundamentally inadequate for post-incident investigation. External black-box recording (1 Hz, all cells/temps/current, RTC timestamps, tamper-evident) is still the single biggest improvement for claims defensibility. This is now an integration-level recommendation, not a firmware one — the firmware provides the data; the integrator needs to capture it.
4. ~~**Demonstrate independent protection layer**~~ → **DONE (P1-01 + P1-02).** BQ76952 autonomous protection configured and verified. IWDG provides MCU recovery. I'm satisfied.

### Priority 2: Strongly recommended (affects premium)

5. **Enable CAN authentication (BMS_CAN_AUTH_ENABLED=1 + implement CMAC)** → Architecture exists. Implementation needed. 5% premium impact.
6. ~~**Implement contactor health monitoring**~~ → **PARTIALLY DONE.** Weld detection improved (500ms window). Still no proactive health monitoring (cycle count, coil current signature, contact resistance trend). Reduced impact.
7. ~~**Add insulation monitoring**~~ → **DONE (P1-06).**
8. ~~**Implement rate-of-rise temperature analysis**~~ → **DONE (P0-04).** Exceeds what I asked for — per-sensor with load correlation.

### Priority 3: Remaining for long-term insurability

9. **Add impedance-based SoH estimation** → STILL RECOMMENDED. The dT/dt trending provides an indirect signal but impedance spectroscopy would close the SoC accuracy exclusion.
10. **Expand NVM to ≥1000 events with RTC timestamps and partitioned log** → STILL RECOMMENDED. IMD trend logging is competing with fault events for the same 64-slot buffer.
11. **NEW: Add periodic BQ76952 register re-verification** — Confirm ENABLE_PROT_A/B/C registers haven't corrupted during operation. Run at thermal task rate (1Hz) with one register per cycle = full re-verify every 3 seconds. Trivial to implement; closes the SEU concern.
12. **NEW: Partition NVM log by type** — Separate ring buffers for fault events vs. trend data vs. reset events. IMD logging at 60s shouldn't compete with thermal event recording.

---

## 7. Revised Unknown Unknowns

### From V1 — Status Update:

| V1 Unknown | V2 Status |
|---|---|
| 7.1 Vibration-induced intermittent I2C | **MITIGATED.** P0-02: 3 consecutive failures → fault latch. Bus recovery attempted on first failure. Intermittent failures no longer invisible — they increment `i2c_fail_count` which persists across cycles. Still no *trend* logging of intermittent I2C, but the transient blind spot is eliminated. |
| 7.2 NTC corrosion reads as lower temp | **MITIGATED.** P0-01: Sentinel detection, plausibility bounds, exact-zero detection, adjacent sensor cross-check. A corroded sensor reading exactly 0.0°C is now caught after 3 scans. A corroded sensor reading a plausible but wrong temperature is harder — but the adjacent cross-check (>20°C delta) catches gross errors. |
| 7.3 Multi-pack DC bus cascade | **UNCHANGED.** No inter-pack coordination visible. Still a risk for multi-pack installations. |
| 7.4 BQ76952 ADC aging | **UNCHANGED.** No ADC calibration verification. 25-year drift concern remains. |
| 7.5 NVM flash endurance | **WORSENED.** IMD trend logging adds 1 write/minute = 1440 writes/day = 525K writes/year. On a 100K-endurance sector, the NVM may wear out in under a year. The ring buffer is still in a single sector. This needs attention. |
| 7.6 Regulatory unknown | **UNCHANGED.** IMO guidelines still evolving. |
| 7.7 Reinsurance treaty constraints | **IMPROVED.** Lower risk profile means treaty caps are less likely to be triggered. |

### NEW Unknown Unknowns in V2:

**7.8 Gas Sensor Calibration Drift**
The firmware reads gas alarm GPIO pins and an analog concentration ADC channel. But gas sensors (electrochemical H₂/CO cells) drift over their 2–3 year service life. The firmware has no gas sensor self-test, no calibration verification, no sensor-age tracking. A sensor that drifts 30% low over 18 months turns a "high alarm" event into a "low alarm" event — or misses it entirely. The firmware trusts the hardware absolutely.

**Confidence: MEDIUM** — Known issue with electrochemical gas sensors in marine environments. Standard mitigation is 6-month bump testing and annual calibration. Not a firmware issue per se, but the firmware could flag "time since last calibration" if it tracked it.

**7.9 dT/dt False Positive Rate**
The dT/dt algorithm uses a 50A current threshold as the boundary between "load-correlated heating" and "anomalous heating." In real marine operations, load can change rapidly (maneuvering, dynamic positioning). A sudden 40A load increase that causes 1.2°C/min heating would trigger the dT/dt alarm even though it's entirely normal I²R heating. The false-positive-to-shutdown ratio will determine whether operators start ignoring alarms.

**Confidence: MEDIUM** — The 50A threshold and 30s sustain requirement provide some filtering, but I haven't seen operational data on false positive rates. Too many false alarms → desensitized operators → disabled alarms → back to square one.

---

## 8. Summary: What Changed, What Didn't

### Material Improvements (Premium Impact: −25 to −45 points)

| Item | V1 State | V2 State | Impact |
|---|---|---|---|
| Gas detection | Not present | Full 2-level with shutdown interlock | **Major** — closes #1 risk driver |
| HW protection independence | BQ76952 unconfigured, shared MCU | BQ76952 autonomous + IWDG | **Major** — satisfies class requirement |
| Insulation monitoring | Not present | IEC 61557-8 compliant with trending | **Significant** — closes ground fault gap |
| dT/dt detection | Not present | Per-sensor, load-correlated, 30s sustain | **Significant** — early warning improvement |
| Ventilation monitoring | Not present | Failure interlock + close inhibit | **Moderate** — closes compliance gap |
| Fire detection/suppression | Not present | Detect + suppress input + manual reset | **Moderate** — correct post-incident behavior |
| Sensor fault detection | Failed reads invisible | Sentinel + plausibility + cross-check + latch | **Significant** — eliminates silent sensor failure |
| I2C comm loss | Non-latching | 3-consecutive → fault_latched + bus recovery | **Moderate** — eliminates 14-cell blind spot |
| Pre-charge bus voltage | Used pack voltage (wrong) | Uses actual bus ADC voltage | **Moderate** — eliminates contactor weld scenario |
| Fault reset security | memset(0) — trivially bypassed | Timer-preserving + rate-limited + logged | **Moderate** — closes Yara's timing attack |
| CAN input validation | None | Type range + negative rejection + reserved check | **Minor** — closes injection vector |
| Sub-zero charging | 5A margin allowed | 0A hard fault | **Minor** — prevents lithium plating |

### Unchanged (Remaining Premium Drivers)

| Item | Status | Premium Impact |
|---|---|---|
| UL 9540A propagation data | Still needed | +12–20% |
| CAN authentication | Stub only (auth disabled) | +3–5% |
| NVM capacity (64 events) | Unchanged | +1–3% |
| NVM timestamps (uptime only) | Unchanged | +1–2% |
| No continuous data logging | Unchanged | +1–3% (integration-level) |
| No SoH estimation | Unchanged | +1–3% |
| Single CAN bus | Unchanged | +1–2% |
| Passive balancing | Unchanged | +1–2% |

---

## 9. Final Assessment

**V1 premium loading: +45–75%**
**V2 premium loading: +18–30%**
**Effective risk reduction: ~55% at midpoint**

This is a well-executed rewrite that addresses the highest-impact findings from my V1 review. The gas detection integration alone justifies a 15–20% loading reduction. Combined with genuine HW protection independence, IMD monitoring, dT/dt detection, and the constellation of sensor fault/plausibility improvements, this is a materially different risk.

The remaining premium is driven primarily by the absence of propagation test data (which is not a firmware issue) and the NVM limitations (which are architectural but fixable).

I would write this policy at the revised loading without hesitation. I would still tell the owner that providing UL 9540A data would save them €1.5M over 10 years in premium — and that a €2K external black-box data logger would save them €500K on their next claim.

The rewrite demonstrates something I rarely see: a development team that understood *why* I was asking for each item, not just *what* I was asking for. The dT/dt load-correlation check, the sensor fault cross-validation, the fault-reset timer preservation — these aren't checkbox implementations. Someone understood the failure modes.

I'm keeping this file for my next battery vessel underwriting as a reference for what "good" looks like at the firmware level.

---

*Catherine Lindqvist*
*Senior Underwriter, Hull & Machinery*
*[Nordic P&I Club]*
