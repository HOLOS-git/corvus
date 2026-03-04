# DELTA REVIEW: Corvus Orca ESS — "Street Smart" Rewrite

**Reviewer:** Mikael, Senior Systems Engineer — EST-Floattech  
**Date:** 2026-03-03  
**Classification:** Internal — Competitive Intelligence  
**Scope:** V2 firmware rewrite, delta from V1 review  
**Baseline:** FULL_REVIEW.md (V1 assessment)

---

## Executive Summary

They read someone's review. Maybe not mine specifically, but someone who thinks like me. The V2 rewrite systematically addresses almost every vulnerability I identified as a competitive attack vector. This is not a cosmetic cleanup — it's a generation-level maturity jump in firmware safety architecture, accomplished without changing hardware.

**The competitive picture has changed materially.** Three of my five primary bid weapons are now neutralized. Two remain. And one finding surprised me enough that I need to brief our own firmware team.

---

## 1. What Changed — The Delta

### 1.1 Insulation Monitoring: NEUTRALIZED ⚠️

**V1:** "Zero ground fault detection in the firmware. CRITICAL — class blocker."

**V2:** `bms_safety_io.c` implements full IMD interface:
- GPIO input from external IMD alarm relay
- ADC input reading insulation resistance (IEC 61557-8 analog output)
- Configurable threshold at 100 kΩ (appropriate for 1kV IT system)
- Alarm → `pack disconnect` + `fault_latched`
- Periodic resistance trending to NVM every 60 seconds

**My assessment:** This closes the class approval gap. It's still reading an external IMD rather than integrating one — our Green Orca has factory-calibrated Bender ISOMETER on-board — but the firmware correctly interprets the signals, logs trends, and acts on alarms. A yard can bolt on a Bender IR155 and this firmware will handle it correctly.

**Competitive impact:** I can no longer claim "Corvus has no insulation monitoring." I can still say "requires external IMD" vs our integrated approach, but that's a weaker argument. The trend logging to NVM is actually something we don't do — I need to check if our firmware logs R_iso history.

**Confidence:** HIGH — code is clear and correct.

### 1.2 dT/dt Thermal Runaway Detection: NEUTRALIZED ⚠️

**V1:** "No dT/dt monitoring. No temperature rate-of-change calculation. No predictive thermal protection."

**V2:** `bms_thermal.c` — entirely new module:
- 30-sample circular buffer per sensor (1 sample/sec)
- dT/dt computed as `(T_now - T_oldest) × 2` → deci-°C/min
- Alarm threshold: 1°C/min (10 deci-°C/min) — matches our threshold exactly
- 30-second sustain requirement before alarm
- Load-correlation check: >50A current increase explains I²R heating, so dT/dt alarm suppressed during legitimate load transients
- Alarm → `fault_latched` + distinct CAN message (0x151) + fire relay output

**What surprised me:** The load-correlation heuristic. This is clever. Naive dT/dt implementations (including our Gen 1) would false-alarm during high-rate discharge ramps — thruster transients on a DP vessel would light up the alarm every time. Corvus's check says "if current went up by 50A+, the temperature rise is probably I²R, not runaway." This reduces false positives without compromising detection.

We added this same logic in Green Orca Gen 2, but we did it with a proper I²R thermal model, not a current-delta heuristic. Their approach is cruder but probably 80% as effective with 10% of the complexity. I'm annoyed at how pragmatic it is.

**What they still lack:** Their 3-thermistor-per-module problem remains. dT/dt on a sensor that's 5 cells away from the thermal event is delayed. With forced-air gradients of 10-15°C inlet-to-outlet, a thermal runaway event at the downstream end might not register 1°C/min at the inlet sensor until the situation is already serious. Our 7-sensor-per-module layout gives us spatial resolution they can't match in firmware.

**Competitive impact:** I can no longer claim "Corvus has no thermal runaway early warning." I can argue about sensor density, but that's hardware, not firmware. The firmware is now competent.

**Confidence:** HIGH.

### 1.3 BQ76952 Hardware Protection Configuration: SIGNIFICANT CHANGE

**V1:** "Confidence MEDIUM-HIGH — code reads safety registers; depends on AFE config."

**V2:** `bq76952_configure_hw_protection()` now explicitly:
- Enters config update mode
- Writes ENABLE_PROT_A: SC + OC1 + OC_CHG + OV + UV (all enabled)
- Writes ENABLE_PROT_B: OTC + OTD + OTF (all thermal protections)
- Writes OV threshold: 4300 mV
- Writes UV threshold: 2700 mV  
- Writes OT charge/discharge: 700 (70.0°C)
- **Read-back verifies every single write**
- Exits config mode
- Fails init entirely if any verify fails

**Why this matters:** In V1, the BQ76952 hardware protection was a question mark — "depends on AFE config we can't see." Now it's explicit in firmware. The BQ76952 will autonomously trip its FET drivers at 4.3V OV, 2.7V UV, and 70°C, regardless of what the STM32F4 MCU is doing. This is a genuine independent hardware safety layer.

The read-back verify on every config write is belt-and-suspenders. If a cosmic ray or I2C glitch corrupts a threshold write, the module fails init entirely rather than running with wrong thresholds. We don't read-back verify our ADBMS6830 config writes. We probably should.

**Competitive impact:** The "independent HW safety" claim is now FULLY legitimate. I can no longer question it. Henrik (whoever that is in their review team) was right to flag the absence as critical.

**Confidence:** HIGH.

### 1.4 Sensor Fault Detection: NEW CAPABILITY

**V1:** Not directly flagged — I focused on the thermal model absence.

**V2:** `bms_monitor.c` now implements:
- Sentinel detection (INT16_MIN from failed I2C reads, not 0°C)
- Plausibility bounds (-40°C to 120°C)
- Exact-zero detection (0.0°C treated as suspicious — corroded wire)
- Cross-check adjacent sensors within module (>20°C delta → warning)
- Inter-module temperature comparison (>20°C between adjacent module averages)
- 3 consecutive bad scans → `sensor_fault` → `fault_latched`
- Faulted sensors excluded from min/max aggregation

**Why this matters competitively:** The original firmware would return 0°C on a dead sensor — the sensor reads "normal" and nobody notices until cells overheat. The 0°C-on-failure bug is industry-endemic; I've seen it in three different competitors' products including one of our older designs. The fact that they explicitly check for exact-zero as suspicious shows someone on their team has real marine field experience (or listened carefully to someone who does).

**Confidence:** HIGH.

### 1.5 I2C Comm Loss → Fault Latch: CRITICAL FIX

**V1:** "I2C mux topology for 22 AFEs is an engineering liability... zero mux health monitoring."

**V2:** `bms_monitor.c` now:
- Tracks per-module I2C failure count
- Attempts bus recovery on first failure (SCL toggle)
- 3 consecutive failures → `comm_loss` = 1 → `fault_latched = true`
- Stack-vs-cells cross-check (|sum - stack| > 2% → plausibility flag)
- dV/dt rate check per cell (>50mV per 10ms → physically impossible → plausibility flag)

**The I2C mux is still there.** They still have 22 BQ76952s behind a TCA9548A. The fundamental architectural vulnerability remains — a mux latch-up still blinds multiple modules. But now they'll detect the blindness within 30ms (3 × 10ms scan period) and shut down. Before, they'd keep running with stale data.

The dV/dt and stack-vs-cells cross-checks are genuinely useful I2C corruption detection. If the mux is feeding garbage data, a 50mV jump in one scan or a 2% divergence between sum-of-cells and stack voltage catches it. This doesn't fix the architecture, but it puts a safety net under it.

**Competitive impact:** I can still attack the I2C mux architecture, but I can no longer say they don't detect failures. The attack now requires a more nuanced argument about failure mode coverage vs. our isoSPI approach.

**Confidence:** HIGH.

---

## 2. What DIDN'T Change — Remaining Vulnerabilities

### 2.1 Single CAN Bus: STILL VULNERABLE ✓

No dual-bus redundancy. Still CAN 2.0B, still custom framing, still 7.7-second cell broadcast cycle. The CAN authentication stub (`bms_can_auth_verify`) tracks sequence counters but AES-128-CMAC is "deferred to Phase 2" — i.e., not implemented. `BMS_CAN_AUTH_ENABLED` defaults to 0.

The input validation improvements (P2-06) are good defensive coding — rejecting negative limits, validating command types, checking reserved bytes — but validation is not authentication. Any device on the bus can still inject commands.

**My assessment:** This remains my strongest competitive lever, especially for DP-class vessels. Single-bus communication in a safety-critical marine system is becoming indefensible as IACS UR E26/E27 tightens.

**Confidence:** HIGH.

### 2.2 I2C Mux Architecture: MITIGATED BUT STILL INFERIOR

Detection is better. Architecture is unchanged. isoSPI daisy-chain is still objectively superior for marine vibration environments. The mux is still a single point of failure; they just fail faster now.

**Confidence:** HIGH.

### 2.3 No Firmware Update Path: STILL ABSENT

No bootloader, no dual-bank flash, no OTA. Still needs JTAG for field updates.

**Confidence:** MEDIUM — could exist in code not shown.

### 2.4 Passive Balancing Only: UNCHANGED

Same BQ76952 internal balance FETs, same 10-50mA bleed current, same 5+ day rebalancing time for deep-cycle marine duty. The rewrite didn't touch `bms_balance.c` at all — marked "Functionally identical to original."

**Confidence:** HIGH.

### 2.5 No Thermal Model: PARTIALLY ADDRESSED

dT/dt detection is reactive-predictive (rate-of-rise). But there's still no forward-looking thermal model — no "you'll hit 65°C in 8 minutes at this load profile." No fan control interface. No cooling system health monitoring. The firmware still has zero awareness of the cooling system.

**Confidence:** HIGH for absence of proactive model. The dT/dt addresses the RUNAWAY case but not the DEGRADATION case.

---

## 3. What Surprised Me

### 3.1 The Review-Driven Development Process

Every function, every threshold, every design decision in the V2 code has a tracking ID (P0-01 through P2-09, CC-01 through CC-10) and attribution to specific reviewers. The code comments read like a forensic audit response:

- "P0-01: Return sentinel on I2C failure, NOT zero" 
- "P0-03: Pre-charge uses BUS voltage from ADC, not pack voltage"
- "P2-05: Timer preservation across fault reset (Yara)"

Someone ran a 6-person expert review panel (Dave, Priya, Catherine, Yara, Henrik, Mikael — wait, that's MY name) and systematically addressed every finding with priority codes. This is how safety-critical firmware development is supposed to work. Most competitors (including us, in earlier product generations) do informal code review and fix what seems important.

The fact that they tracked and tagged every change to its reviewer origin means they can show a class society exactly who found what and how it was addressed. That's TÜV/IEC 61508 territory.

### 3.2 The Contactor Pre-Charge Fix

**V1:** I didn't flag this. I should have.

**V2:** The original code used pack voltage as the pre-charge target. This is wrong in a multi-pack system — if other packs are already on the DC bus at a different voltage, you need to match the BUS voltage, not your own stack voltage. Closing a contactor with a 30V differential across it at 1000V will arc-weld the contacts.

The V2 code reads `ADC_BUS_VOLTAGE` and verifies `|pack - bus| < BMS_VOLTAGE_MATCH_MV` before closing the main contactor. This is a fix that prevents a physically destructive failure mode.

I need to check our own pre-charge logic. We parallel packs on a shared DC bus. If we're making the same mistake...

### 3.3 Fault Reset Rate Limiting

The original firmware did `memset(prot, 0, sizeof(*prot))` on fault reset — zeroing all 308 OV timers. As one reviewer noted, an operator could reset every 60 seconds and prevent any fault from ever latching (each timer needs 5 seconds to trip, but gets zeroed every 60 seconds before accumulating enough).

V2 preserves integrator timers across reset and limits to 3 resets per hour. Fire/suppression events require manual intervention (no CAN reset). This is the kind of defense-in-depth thinking that separates safety-rated firmware from prototype firmware.

### 3.4 The Safety I/O Module is Complete

`bms_safety_io.c` is entirely new — gas detection (H₂/CO), ventilation monitoring, fire detection/suppression interface, and IMD. All with proper state machines, interlock logic, and CAN reporting. The contactor close is inhibited if vent failure, gas alarm, fire, or IMD alarm is active.

This module alone addresses four of the six reviewers' "SHOWSTOPPER" or "CRITICAL" findings. It integrates with the state machine cleanly — `bms_state_run` checks `bms_safety_io_emergency_shutdown()` and `bms_safety_io_inhibit_close()` as gate conditions.

---

## 4. Revised Competitive Assessment

### What We Can Still Win On

| Attack Vector | V1 Strength | V2 Strength | Notes |
|--------------|-------------|-------------|-------|
| Dual-CAN redundancy | STRONG | STRONG | Unchanged. Best lever for DP-class. |
| CAN authentication | STRONG | STRONG | Stub only. IACS E26/E27 compliance gap. |
| isoSPI vs I2C mux | STRONG | MODERATE | They detect failures now. Architecture still inferior. |
| Active balancing | MODERATE | MODERATE | Unchanged. Long-term capacity argument. |
| Thermal model (proactive) | MODERATE | WEAK | dT/dt handles runaway. No degradation model. |
| EtherCAT option | MODERATE | MODERATE | Cell broadcast still 7.7s stale. |

### What We've Lost

| Former Attack Vector | V1 Strength | V2 Strength | Notes |
|---------------------|-------------|-------------|-------|
| No insulation monitoring | STRONG | GONE | IMD interface implemented. |
| No dT/dt detection | STRONG | GONE | Full implementation with load correlation. |
| No HW safety independence | MODERATE | GONE | BQ76952 protection fully configured + verified. |
| No sensor fault detection | MODERATE | GONE | Comprehensive, better than our Gen 1. |
| comm_loss doesn't latch | MODERATE | GONE | 3-failure latch with bus recovery. |

### Net Assessment

**V1 conclusion:** "A system I'd beat."  
**V2 conclusion:** "A system I'd have to work harder to beat."

The rewrite closes the safety gap to the point where the competitive differentiation is now primarily architectural (isoSPI vs I2C, dual-CAN vs single-CAN, active vs passive balancing) rather than firmware maturity. In V1, the firmware was a generation behind our Gen 2. In V2, the firmware is competitive with our Gen 2 on safety features, though our hardware architecture remains superior.

**The uncomfortable truth:** If Corvus ships this firmware to their existing fleet as a software update, every Orca ESS in the field gets dT/dt detection, IMD integration, sensor fault detection, and HW safety configuration — without a single hardware change. Our architectural advantages (isoSPI, dual-CAN, liquid cooling, 7-sensor thermal) require hull-in work to retrofit. Software beats hardware for upgrade velocity.

---

## 5. Revised Strategic Recommendations

### For EST-Floattech

1. **Double down on dual-CAN and IACS E26/E27 compliance.** This is our strongest remaining differentiator. Corvus cannot fix single-bus architecture in firmware. Press this in every DP-class bid.

2. **Stop leading with IMD and dT/dt in bids against Corvus.** If they've shipped V2, these arguments will backfire — their technical evaluator will demonstrate the capability and we'll look uninformed. Verify what's actually shipping before making claims.

3. **Adopt their fault-reset rate limiting.** Check if our firmware has the same memset-on-reset vulnerability. If Yara (whoever that is) found it in their code, it might exist in ours.

4. **Check our pre-charge logic for multi-pack bus voltage matching.** The P0-03 fix they implemented is a real safety issue. If we're using pack voltage instead of bus voltage for pre-charge target, we have the same contactor-weld risk.

5. **Monitor whether this firmware ships.** V2 as a codebase is impressive. Whether it passes Corvus's internal validation, class re-approval, and actually reaches production is a different question. There's typically 6-18 months between "firmware complete" and "type-approved and shipping."

6. **Prepare Gen 3 architecture with EtherCAT + dual redundancy.** If Corvus closes the CAN gap (Phase 2 mentions authentication), our last major differentiator narrows to bus topology. EtherCAT gives us 10× bandwidth advantage that CAN 2.0B cannot match regardless of authentication.

---

## 6. Confidence Statement — V2 Delta

| Finding | Confidence | Change from V1 |
|---------|-----------|----------------|
| IMD now implemented in firmware | **HIGH** | Was CRITICAL gap, now closed |
| dT/dt detection functional | **HIGH** | Was absent, now present and competent |
| BQ76952 HW protection is now verified | **HIGH** | Was MEDIUM-HIGH, now confirmed |
| Sensor fault detection comprehensive | **HIGH** | Was not assessed, now industry-grade |
| I2C mux still architectural weakness | **HIGH** | Mitigated, not eliminated |
| Single CAN remains primary vulnerability | **HIGH** | Unchanged |
| CAN auth not implemented (stub only) | **HIGH** | Sequence counters added, CMAC deferred |
| No firmware update mechanism | **MEDIUM** | Unchanged |
| Passive balancing only | **HIGH** | Unchanged |
| No proactive thermal model | **HIGH** | dT/dt is reactive-predictive, not proactive |
| Software quality is now safety-grade | **HIGH** | NEW: review-traced, defense-in-depth |
| Competitive gap has narrowed significantly | **HIGH** | Was "generation behind," now "architectural differences" |

---

*The firmware team that did this rewrite knows what they're doing. The review-driven process, the systematic fix prioritization, the pragmatic load-correlation heuristic in dT/dt — this is experienced safety engineering. If I had to guess, they brought in someone with IEC 61508 or automotive ASIL experience.*

*Our hardware architecture is still better. But the gap between "better architecture with average firmware" and "adequate architecture with excellent firmware" is narrower than marketing thinks.*

*I still wouldn't trade our isoSPI for their I2C mux. But I'd trade our Gen 1 firmware for their V2 in a heartbeat.*

— Mikael
