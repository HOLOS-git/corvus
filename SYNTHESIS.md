# Mockingbird Synthesis — Corvus Orca ESS Firmware Review

**Date**: 2026-03-03  
**Panel**: 6 adversarial reviewers (Field Tech, Insurance Adjuster, Competitor Engineer, Class Surveyor, Pentester, Thermal Engineer)  
**Subject**: Corvus Orca ESS BMS Firmware (STM32F4, BQ76952 AFE, 22 modules × 14 cells = 308 SE)

---

## Executive Summary

**Consensus: The firmware is architecturally competent but not deployable.**

All six reviewers independently concluded that the BMS firmware demonstrates strong design fundamentals — per-cell voltage monitoring with leaky integrator protection, clean 7-mode state machine, HW/SW protection layer separation, fixed-point arithmetic throughout, and conservative voltage thresholds. The engineering intent is correct.

All six reviewers also independently concluded the system **cannot be deployed on a vessel** in its current state. The reasons converge on a remarkably consistent set of gaps:

1. **No independent hardware safety layer** (flagged by 5/6 reviewers) — both protection paths run on the same MCU
2. **No gas detection or ventilation integration** (flagged by 5/6 reviewers) — mandatory for classification
3. **No thermal runaway early warning** (flagged by 5/6 reviewers) — no dT/dt, no gas sensing, no predictive thermal model
4. **No CAN bus authentication** (flagged by 6/6 reviewers) — any device on the bus has full command authority
5. **No insulation monitoring** (flagged by 5/6 reviewers) — mandatory for 1000Vdc marine systems
6. **Temperature sensor failure reads as normal** (flagged by 4/6 reviewers) — a dead sensor returns 0°C, a plausible value

The firmware is best characterized as a **bench demonstration of correct BMS architecture** that lacks the failure handling, environmental hardening, safety integration, and security controls required for maritime deployment.

**Verdicts by reviewer:**
| Reviewer | Verdict |
|----------|---------|
| Field Tech (Dave) | **NO** — Do not commission |
| Insurance (Catherine) | **CONDITIONAL** — +45-75% premium loading, mandatory exclusions |
| Competitor (Mikael) | Significant competitive vulnerabilities; a generation behind on thermal/security |
| Class Surveyor (Henrik) | **CONDITIONAL** — Cannot recommend for type approval; 6 FAIL, 3 PARTIAL out of 19 requirements |
| Pentester (Yara) | **CRITICAL RISK** — Zero intentional security controls; worst posture seen on safety-critical maritime system |
| Thermal Engineer (Priya) | **INADEQUATE** — No thermal model in firmware, no runaway detection, no cooling health monitoring |

---

## Cross-Cutting Findings

These issues were identified by 3+ reviewers independently — the highest-confidence findings.

### CC-01: No CAN Bus Authentication (6/6 reviewers)

Every reviewer flagged the completely unauthenticated CAN bus. `bms_can_rx_process()` accepts any frame on ID 0x200 with no MAC, no sequence counter, no source validation. A $30 CAN adapter gives full BMS command authority.

| Reviewer | Framing |
|----------|---------|
| Dave | "Any device on the bus can send EMS commands — I've seen CAN bus miswiring cause exactly this" |
| Catherine | "Who is responsible when a spoofed CAN message prevents safe state? Nobody — the insurer pays" |
| Mikael | "Zero authentication. Not J1939, not CANopen. Custom framing with no security" |
| Henrik | "Safety functions cannot be defeated by single failures. Unauthenticated command bus is a single-point vulnerability" |
| Yara | "CVSS 9.8 — any device on CAN has full unauthenticated command authority over a system that can cause fire" |
| Priya | (Referenced via security implications of fire system integration gap) |

**Confidence: CERTAIN** — verified by direct code inspection by all reviewers.

### CC-02: No Independent Hardware Safety Layer (5/6 reviewers)

Both SW protection (`bms_protection_run`) and HW safety (`bms_protection_hw_safety`) execute on the same STM32F4 MCU. The BQ76952 AFE has autonomous protection capability, but `bq76952_write_data_memory()` is never called with protection enable registers — the AFE's independent protection may not be configured. No hardware watchdog (IWDG) is configured. Contactor control is entirely software-mediated.

| Reviewer | Key Point |
|----------|-----------|
| Dave | "The HW/SW separation is architectural, not physical — same MCU, same bus" |
| Catherine | "The HW safety layer shares MCU with SW layer — both run in `bms_protection.c`" |
| Henrik | "SHOWSTOPPER — MCU hang kills both layers simultaneously. BQ76952 protection not configured" |
| Yara | "BQ76952 safety registers are processed by STM32F4 software — they share the same SPOF" |
| Priya | "Both SW and HW paths share the same I2C bus — a bus fault kills both" |

**Confidence: HIGH** — code inspection confirms no IWDG, no BQ76952 protection configuration, no independent trip path.

### CC-03: No Gas Detection / Ventilation Integration (5/6 reviewers)

Zero references to gas detection, ventilation monitoring, or battery room atmosphere management anywhere in the codebase. This is a classification mandatory requirement (DNVGL-RU-SHIP Pt.6 Ch.2 Sec.1 §1.4).

| Reviewer | Key Point |
|----------|-----------|
| Catherine | "Gas detection gap is the #1 risk driver — 15-20% premium loading" |
| Mikael | "Post-Ytterøyningen, every class society asks about thermal runaway detection. This firmware has none" |
| Henrik | "SHOWSTOPPER — zero references to gas detection in entire codebase" |
| Yara | (Not in scope but noted fire system integration gap) |
| Priya | "NMC cells vent flammable gases before reaching thermal runaway temperatures. Gas detection provides 30-120s earlier warning than temperature sensors" |

**Confidence: HIGH** — comprehensive code search confirms complete absence.

### CC-04: No Thermal Runaway Early Warning / dT/dt Detection (5/6 reviewers)

The firmware monitors absolute temperature thresholds (65°C/70°C) but has no rate-of-rise detection, no predictive thermal model, and no cooling system health monitoring. A cell entering thermal runaway would only be detected when adjacent thermistors cross 65°C — by which time propagation may already be underway.

| Reviewer | Key Point |
|----------|-----------|
| Dave | "No dT/dt monitoring. Purely reactive threshold detection" |
| Catherine | "30-120 second detection gap between cell failure and BMS detection — this is where €500K becomes €50M" |
| Mikael | "No thermal model in firmware whatsoever. EST-Floattech runs a 3-node thermal model per module at 1 Hz" |
| Henrik | "No rate-of-rise temperature detection — most reliable early indicator of thermal runaway" |
| Priya | "dT/dt detection is the single most cost-effective safety improvement. ~50-80 lines of C code. No hardware changes" |

**Confidence: HIGH** — confirmed by line-by-line code review by multiple reviewers.

### CC-05: No Insulation Monitoring (5/6 reviewers)

This is a 1000Vdc system with zero ground fault detection or insulation resistance monitoring. No code, no HAL interface, no ADC channel for IMD.

| Reviewer | Key Point |
|----------|-----------|
| Dave | "Any class surveyor would laugh this out of the room. Ytterøyningen fire started with coolant providing a conductive path at 1000V" |
| Catherine | "A ground fault on one pole combined with a second fault creates a short circuit path that bypasses the BMS entirely" |
| Mikael | "DNVGL-RU-SHIP Pt.6 Ch.2 Sec.1 requires insulation monitoring for HV IT systems. Class approval blocker" |
| Henrik | "FAIL — No insulation resistance measurement" |
| Yara | (Not primary scope) |

**Confidence: HIGH** — complete absence confirmed; class rules are unambiguous.

### CC-06: Temperature Sensor Failure Reads as Normal (4/6 reviewers)

`bq76952_read_temperature()` returns 0 on I2C failure → 0°C after conversion. A dead sensor looks like a cold module. Protection code never trips on a sensor that's failed open.

| Reviewer | Key Point |
|----------|-----------|
| Dave | "CRITICAL — I've replaced dozens of corroded marine thermistor connections. Every one read 'normal' until it read 'open circuit'" |
| Catherine | "Saltwater corrosion on NTC thermistors reads as LOWER temperature. No plausibility check exists" |
| Mikael | "Three sensors per 14-cell module means you're blind to hot spots at the cell-to-cell level" |
| Priya | "A failed sensor reading 25°C while the cell is at 150°C is a silent killer" |

**Confidence: HIGH** — verified in code; physically grounded failure mode.

### CC-07: No Firmware Update Mechanism (4/6 reviewers)

No bootloader, no dual-bank flash, no CRC verification, no rollback capability. Field updates require JTAG/SWD.

| Reviewer | Key Point |
|----------|-----------|
| Dave | "CRITICAL — I have personally seen firmware updates brick production BMS systems" |
| Mikael | "Every competitor has over-CAN authenticated firmware updates" |
| Henrik | (Implicit in software verification gap) |
| Yara | "No secure boot, no firmware signing — an attacker can install persistent malicious firmware" |

**Confidence: HIGH** — complete absence confirmed.

### CC-08: Inadequate NVM Fault Logging (4/6 reviewers)

64-entry ring buffer with 8-byte events. Uptime-only timestamps (reset on power cycle). No CRC, no integrity protection. No continuous data logging.

| Reviewer | Key Point |
|----------|-----------|
| Dave | "A tech at 2am gets a number from 1-3 and a cell index. No human-readable descriptions" |
| Catherine | "Root cause determination: likely impossible from BMS data alone. The insurer pays" |
| Henrik | "PARTIAL — no integrity protection" |
| Yara | "CVSS 6.5 — NVM tamperable, no HMAC, no monotonic counter" |

**Confidence: HIGH** — verified in code.

### CC-09: Communication Loss Does Not Latch Fault (3/6 reviewers)

`comm_loss` flag is set but `fault_latched` is never set. The pack stays CONNECTED with unmonitored cells.

| Reviewer | Key Point |
|----------|-----------|
| Dave | "CRITICAL — 14 cells at unknown voltage and temperature with nobody watching" |
| Catherine | "A connector that fails 1 in 100 reads won't trigger the fault but will miss real cell voltage excursions" |
| Henrik | (Noted under HW safety layer assessment) |

**Confidence: HIGH** — verified in code.

### CC-10: Pre-charge Uses Pack Voltage Instead of Bus Voltage (3/6 reviewers)

`bms_contactor_request_close()` uses `pack->pack_voltage_mv` instead of reading `ADC_BUS_VOLTAGE`. Voltage mismatch at contactor closure can weld contactors.

| Reviewer | Key Point |
|----------|-----------|
| Dave | "CRITICAL — 30V differential across the contactor at closure. Contactor welds. Permanent 1000V connection you can't break" |
| Mikael | (Noted in contactor analysis) |
| Henrik | (Noted under parallel string inrush) |

**Confidence: HIGH** — verified in code; ADC_BUS_VOLTAGE defined but never used.

---

## Unique Findings by Mask

These important issues were caught by only one reviewer — demonstrating the value of diverse perspectives.

### Field Tech (Dave) — Unique Findings

- **EMS watchdog only active in CONNECTED/CONNECTING**: Pack sits in READY indefinitely with dead EMS. No timeout to POWER_SAVE.
- **Contactor weld detection window too short (200ms)**: Misses intermittent welding. No current decay rate check.
- **No stack-vs-cells cross-check**: `stack_mv` is read and stored but never compared against sum of individual cell voltages.
- **Imbalance warning has no hysteresis**: Flashes on/off every 220ms under dynamic load — nuisance alarm training.
- **Balancing disabled during FAULT state**: Can't self-correct imbalance that caused the fault.
- **I2C mux latch-up risk**: No mux reset or power-cycle mechanism for TCA9548A.

### Insurance Adjuster (Catherine) — Unique Findings

- **Total loss scenario quantification**: €500K (pack replacement) to €50M+ (vessel loss + pollution) depending on detection gap.
- **Warranty gap analysis**: Liability boundaries drawn so every likely failure falls in the gap between manufacturer and integrator — insurer pays.
- **Claims investigation readiness**: NVM data insufficient for root cause determination, subrogation, or defense against warranty exclusion claims.
- **Premium loading quantification**: +45-75% over baseline H&M rate with specific breakdowns per risk factor.
- **Multi-pack cascade on DC bus**: Pack A faults → remaining packs see instantaneous load increase → possible cascade trip → dead ship.

### Competitor Engineer (Mikael) — Unique Findings

- **Patent-implementation gap**: Patents describe optical communication, liquid cooling, segregated gas management. Firmware implements I2C muxes, forced-air thresholds, single CAN bus. Either older product generation or aspiration-vs-reality gap.
- **BQ76952 vs ADBMS6830 analysis**: isoSPI daisy-chain eliminates I2C mux nightmare; 18 ICs vs 22.
- **Per-module CC2 current unused**: BQ76952 coulomb counter driver exists (`bq76952_read_current`) but is never called in monitor code. Low-hanging fruit for module-level diagnostics.
- **7.7-second cell broadcast cycle**: EMS operating on data up to 7.7s stale during transients.
- **Passive balancing inadequacy**: At 50mA balance current on 128Ah cells, correcting 5% SoC delta takes 128 hours of continuous balancing.

### Class Surveyor (Henrik) — Unique Findings

- **Comprehensive rule compliance matrix**: 19-item assessment against DNVGL-RU-SHIP Pt.6 Ch.2 Sec.1 — 10 PASS, 3 PARTIAL, 6 FAIL.
- **IEC 62619 propagation gap (Annex A)**: Normative for maritime applications; no propagation mitigation strategy visible.
- **OC charge protection incomplete**: Only activates below 0°C. At ambient temperatures, no overcurrent charge trip exists — only advisory derating.
- **Conditions of Class**: 5 hold points (HC-01 through HC-05), 4 major findings (MF-01 through MF-04), 4 observations.
- **Type approval envelope unknown**: Cannot assess whether firmware falls within approved configuration without TAP certificate.

### Pentester (Yara) — Unique Findings

- **Complete trust boundary map**: 7 trust boundaries identified, 3 controlled by firmware — none enforced.
- **6 detailed attack scenarios**: Ghost EMS takeover, Silent Kill firmware persistence, CAN flood DoS, maintenance laptop pivot, cell voltage gaslighting, protection juggling.
- **Protection timer reset attack**: `bms_protection_reset()` does `memset(prot, 0, sizeof(*prot))` — all 308 OV timers, 308 UV timers, 66 OT timers zeroed. Timing attack: reset every 60s to prevent any fault from latching.
- **Negative current limit bypass**: `int16_t` × 1000 — negative charge limit satisfies "less than" clamp, bypassing the downward-only safety intent.
- **BQ76952 supply chain risk**: Device number check (0x7695) is trivially spoofable by counterfeit IC. No firmware version verification.
- **IEC 62443 compliance**: SL-0 achieved (no security). SL-2 minimum required for maritime ESS.
- **Debug interfaces unlocked**: STM32F4 at RDP Level 0 — $4 ST-Link clone gives full firmware extraction and replacement.

### Thermal Engineer (Priya) — Unique Findings

- **Quantified thermal model errors**: Hotspot temperature underestimated by 10-20°C at nominal; cooling capacity overestimated by 1.6-4× for marine conditions.
- **800 W/°C cooling coefficient critique**: Requires ~1000+ CFM airflow over 20m² effective surface — achievable in clean-room conditions, not sustainable in marine engine room with salt fouling.
- **Propagation timeline**: Cell failure (t=0) → detectable by BMS (t=10-20min) → module fire (t=25-60min) → pack fire (t=30-90min without barriers). 1.5-2.4 MJ total chemical energy ≈ 0.4-0.6 kg TNT equivalent.
- **Sensor thermal lag quantified**: NTC time constant 5-30s. During rapid events (1-10°C/s runaway onset), sensor lags by 5-20°C.
- **Hotspot-to-sensor delta analysis**: Normal operation: 6°C. High rate: 15°C. Degraded cell: 28°C+. Thermal runaway: infinite.
- **Fire suppression interaction**: Water mist most effective but creates HF gas via LiPF₆ reaction. Re-ignition expected within minutes to hours for NMC.

---

## Disagreements / Tensions

### Tension 1: OT Threshold — Conservative or Risky?

- **Henrik (Class Surveyor)**: 65°C/70°C thresholds with 5s delay — PASS for class requirements
- **Mikael (Competitor)**: "65°C OT with 5s delay is hot for NMC 622. Samsung SDI recommends 60°C max. At 65°C for 5 seconds, you're already doing irreversible damage"
- **Priya (Thermal)**: The real problem isn't the threshold — it's the 10-20°C gap between hottest cell and nearest sensor. Effective protection temperature is 75-85°C, not 65°C
- **Resolution**: Not contradictory — the threshold is compliant but the *effective* protection temperature (threshold + sensor delta) may be inadequate. Priya's analysis resolves the apparent disagreement.

### Tension 2: BQ76952 as Independent Safety Layer

- **Henrik**: "Does NOT qualify as independent in current implementation" (no configuration of AFE autonomous protection)
- **Mikael**: "BQ76952 HW safety provides independent layer" (MEDIUM-HIGH confidence)
- **Catherine**: "BQ76952 ASIC protections ARE independent" (HIGH confidence)
- **Resolution**: The BQ76952 *can* provide independent protection, but the firmware doesn't configure it to do so. Henrik is correct about current implementation. Catherine and Mikael are correct about capability. The fix (configure ENABLE_PROT_A/B/C registers) would satisfy all three.

### Tension 3: Is This the Complete System?

- **Mikael**: "This firmware might be the module controller only, with a separate pack controller handling optical safety loops, liquid cooling, and gas management"
- **Dave**: Reviews as if this is the complete BMS
- **Henrik**: Notes "Is this the only BMS layer?" as unknown unknown
- **Resolution**: Cannot determine from available code. If a separate safety controller exists, many "missing" features may be addressed at the system level. However, the firmware-level gaps (sensor failure detection, comm_loss latching, pre-charge voltage) remain regardless.

### Tension 4: Passive vs Active Balancing

- **Mikael**: "Active balancing would recover 2-5% usable capacity. Passive is dated"
- **Catherine**: "Passive balancing only — indicates cost optimization; 3-5% premium loading"
- **Dave**: Does not flag balancing as a significant issue
- **Resolution**: Passive balancing matches Corvus's own patent (US11171494B2). It's a design choice, not a defect. The competitive and insurance perspectives frame it negatively; the field tech doesn't see it as a safety issue. Prioritize lower than safety-critical items.

### Tension 5: CAN Bus — Adequate or Obsolete?

- **Mikael**: "CAN 2.0B is dated. 7.7s cell broadcast is unacceptable. Industry moving to EtherCAT"
- **Dave**: Doesn't flag CAN speed as an issue (pragmatic — CAN is what's on every vessel)
- **Henrik**: Flags lack of redundancy and authentication, not speed
- **Resolution**: CAN is adequate for current market integration. The authentication gap is the critical issue, not bus speed. Redundancy matters for DP-class vessels. EtherCAT is a competitive differentiator, not a safety requirement.

---

## Confidence Map

| Finding | Dave | Catherine | Mikael | Henrik | Yara | Priya |
|---------|------|-----------|--------|--------|------|-------|
| No CAN authentication | HIGH | MEDIUM | HIGH | HIGH | CERTAIN | — |
| HW safety not independent | — | HIGH | MEDIUM-HIGH | HIGH | HIGH | MEDIUM |
| No gas detection | HIGH | HIGH | HIGH | HIGH | — | MEDIUM |
| No dT/dt runaway detection | — | MEDIUM | HIGH | HIGH | — | HIGH |
| No insulation monitoring | HIGH | HIGH | HIGH | HIGH | — | — |
| Temp sensor failure = 0°C | HIGH | MEDIUM | — | — | HIGH | HIGH |
| No firmware update mechanism | HIGH | — | MEDIUM | — | HIGH | — |
| NVM logging inadequate | MEDIUM | HIGH | — | HIGH | CERTAIN | — |
| Comm loss doesn't latch fault | HIGH | MEDIUM | — | — | — | — |
| Pre-charge uses wrong voltage | HIGH | — | — | — | — | — |
| No cooling health monitoring | — | HIGH | HIGH | — | — | HIGH |
| Protection timer reset attack | — | — | — | — | CERTAIN | — |
| Debug interfaces unlocked | — | — | — | — | HIGH | — |
| 800 W/°C cooling overestimate | — | — | MEDIUM | — | — | HIGH |
| Patent-implementation gap | — | — | MEDIUM-HIGH | — | — | — |

---

## Unknown Unknowns Synthesis

Aggregated from all 6 reviewers, deduplicated, and prioritized by potential impact.

### Tier 1: Could Change the Entire Assessment

1. **Separate safety controller existence** (Mikael, Henrik, Dave) — If a pack-level safety controller with optical safety loops, liquid cooling control, and gas management exists separately, many "missing" features are addressed. Impact: Would change assessment from "significant gaps" to "module controller adequate."

2. **Hardware schematic / BQ76952 FET wiring** (Henrik, Priya) — If BQ76952 CHG/DSG outputs are wired to contactor control through hardware logic, independence finding changes from SHOWSTOPPER to MAJOR. If a hardware safety relay exists, same effect.

3. **Thermal runaway propagation barrier design** (Priya, Catherine) — If proper barriers exist (mica sheets, intumescent materials), propagation risk changes from "catastrophic and near-certain" to "potentially manageable." Single largest unknown for total loss probability.

### Tier 2: Could Significantly Affect Specific Findings

4. **MCU power loss behavior** (Henrik) — If contactor coils de-energize on MCU power loss (fail-safe design), this mitigates the independence concern via hardware architecture rather than firmware.

5. **BQ76952 configuration data memory** (Mikael, Henrik) — If AFE internal OV/UV/OT/SC thresholds are factory-configured, there IS a genuine independent trip path at ASIC level.

6. **I2C mux latch-up under power transients** (Dave) — Mux failure during contactor operations could blind multiple modules simultaneously. No reset mechanism visible.

7. **IT/OT network segmentation** (Yara) — If CAN bus is completely isolated from vessel IT network, remote attack scenarios are significantly less likely.

8. **Actual cell chemistry and supplier** (Mikael, Priya) — Different NMC 622 cells have different thermal characteristics. 65°C OT could be conservative or lenient depending on supplier.

### Tier 3: Operational Concerns Requiring Physical Validation

9. **Vibration-induced connector fatigue** (Dave, Catherine, Priya) — I2C connectors, thermistor wires, and thermal interface materials degrade under marine vibration. Progressive failure invisible to BMS.

10. **Salt air corrosion on NTC thermistors** (Dave, Catherine) — Corrosion increases NTC resistance, reading as LOWER temperature. BMS believes pack is cooler than reality.

11. **Cooling duct fouling rate in marine air** (Priya) — Determines how fast 800→350 W/°C degradation occurs. Could be months or years.

12. **Multi-pack cascade on shared DC bus** (Catherine, Mikael) — Pack fault → load redistribution → cascade trip → dead ship. No coordination protocol visible.

13. **RTOS timing under full I2C load** (Dave, Yara) — Task stubs with commented-out code. Stack overflow on monitor task would be silent and catastrophic.

14. **Electrolyte vapor accumulation** (Priya) — Low-level off-gassing in poorly ventilated space can reach flammable concentrations over months.

15. **BQ76952 ADC aging over vessel life** (Catherine, Mikael) — 1% drift shifts measurements by ~42mV. Protection thresholds silently erode over 10+ years.

16. **Condensation during rapid temperature transients** (Priya) — Arctic-to-tropical route changes cause condensation → corrosion → eventual internal shorts.

17. **CAN↔Ethernet gateway security posture** (Yara) — The bridge between CAN and ship network is the highest-value attack target and is completely unassessed.

18. **Maintenance laptop as air-gap bridge** (Yara) — Demonstrated attack pattern in maritime OT. The laptop IS physical access.

19. **Contactor coil power supply independence** (Dave) — Blown fuse on coil supply means contactor stays in last state. No separate supply watchdog visible.

20. **Pack enclosure venting design** (Priya) — If gas accumulates inside pack (no vent path), explosion hazard from single cell vent event.

---

*Synthesis completed 2026-03-03. Based on reviews by 6 independent adversarial perspectives with combined coverage of field operations, insurance/risk, competitive analysis, classification compliance, cybersecurity, and thermal engineering.*
