# Implementation Plan — Corvus Orca ESS Firmware

**Date**: 2026-03-03  
**Based on**: Mockingbird 6-reviewer synthesis  
**Approach**: Phased remediation from safety-critical to competitive differentiation

---

## Phase 0: Critical Safety (Must-Fix Before ANY Testing)

Issues that could cause harm to persons or property if unfixed.

### P0-01: Temperature Sensor Failure Detection

- **Description**: `bq76952_read_temperature()` returns 0 on I2C failure → 0°C after conversion. A dead sensor looks normal. Corroded thermistor wires (common in marine environments) silently defeat thermal protection.
- **Flagged by**: Dave (CRITICAL), Catherine (HIGH), Priya (HIGH), Yara (HIGH)
- **Effort**: 2-3 days
- **Dependencies**: None
- **Acceptance criteria**:
  - Failed temperature read returns sentinel value (e.g., `INT16_MIN`)
  - Any reading below -40°C or exactly 0°C on 3+ consecutive scans → sensor fault
  - Sensor fault on any module sets `fault_latched = true`
  - Cross-check: if one sensor in module reads 25°C and adjacent reads 0°C exactly → flag

### P0-02: Communication Loss Must Latch Fault

- **Description**: `comm_loss` flag is set but `fault_latched` is never set. Pack stays CONNECTED with 14+ unmonitored cells at unknown voltage/temperature.
- **Flagged by**: Dave (CRITICAL), Catherine (MEDIUM)
- **Effort**: 1 day
- **Dependencies**: None
- **Acceptance criteria**:
  - 3 consecutive I2C failures on any module → `fault_latched = true`
  - I2C bus recovery sequence attempted before declaring failure
  - Once latched, requires manual reset (not auto-clear)

### P0-03: Pre-charge Must Use Bus Voltage

- **Description**: `bms_contactor_request_close()` uses pack voltage instead of bus-side voltage. Voltage mismatch at contactor closure can weld contactors (permanent, unbreakable 1000V connection).
- **Flagged by**: Dave (CRITICAL)
- **Effort**: 1-2 days
- **Dependencies**: None (ADC_BUS_VOLTAGE already defined in HAL)
- **Acceptance criteria**:
  - Read actual bus voltage via `hal_adc_read(ADC_BUS_VOLTAGE)` before/during pre-charge
  - Pre-charge target = 95% of bus voltage (not pack voltage)
  - |pack_voltage - bus_voltage| must be < `BMS_VOLTAGE_MATCH_MV` before closing main contactor
  - Abort with specific error code if voltage match fails after timeout

### P0-04: Implement Thermal Rate-of-Rise Detection (dT/dt)

- **Description**: No rate-of-change temperature monitoring. Firmware is purely reactive (threshold-based). dT/dt > 1°C/min with no corresponding load change is the single best early indicator of thermal runaway, providing 5-15 minutes additional warning.
- **Flagged by**: Catherine (HIGH), Mikael (HIGH), Henrik (MAJOR), Priya (CRITICAL)
- **Effort**: 3-5 days (~50-80 lines of C code, no hardware changes)
- **Dependencies**: None
- **Acceptance criteria**:
  - Per-sensor dT/dt computed every scan cycle, filtered with 30s moving average
  - Alarm at dT/dt > 1°C/min sustained >30s with no corresponding current increase
  - dT/dt alarm triggers fault latch independently of absolute temperature threshold
  - dT/dt alarm communicated on CAN as distinct alarm type

### P0-05: Sub-Zero Charging Hard Fault

- **Description**: Current limit derates to 0A below 5°C, but no fault if charging actually occurs below freezing. If EMS ignores the limit, NMC cells lithium-plate silently. The OC charge formula allows up to 5A at sub-zero due to the 5A margin.
- **Flagged by**: Dave (HIGH)
- **Effort**: 1 day
- **Dependencies**: None
- **Acceptance criteria**:
  - If `pack_current_ma > 0` AND `min_temp_deci_c < 0` for >5 seconds → `fault_latched = true`
  - 0A margin (not 5A) on OC charge threshold below freezing

---

## Phase 1: Classification Readiness (Minimum Viable for DNV Survey)

### P1-01: Configure BQ76952 Autonomous Hardware Protection

- **Description**: BQ76952 has independent OV/UV/OT/SC protection with FET gate drivers that can trip without MCU. Protection enable registers (`ENABLE_PROT_A/B/C`) are defined in code but never written. This is the single most important change for HW safety layer independence.
- **Flagged by**: Henrik (SHOWSTOPPER), Mikael (MEDIUM-HIGH), Catherine (HIGH), Priya (MEDIUM)
- **Effort**: 3-5 days (configuration + verification testing)
- **Dependencies**: None
- **Acceptance criteria**:
  - `bq76952_write_data_memory()` called during init with protection enable registers
  - BQ76952 OV/UV/OT/SC thresholds set to HW safety values (4.300V/2.700V/70°C)
  - Test: MCU forced into hard fault → BQ76952 independently trips within configured delay
  - Read-back verify all protection configuration after write

### P1-02: Enable STM32F4 Hardware Watchdog (IWDG)

- **Description**: No hardware watchdog configured. Firmware hang leaves both protection layers dead and contactors in last state.
- **Flagged by**: Henrik (SHOWSTOPPER), Yara (implicit)
- **Effort**: 1 day
- **Dependencies**: None
- **Acceptance criteria**:
  - IWDG configured with timeout ≤ 100ms (10× protection loop period)
  - Fed from main protection loop
  - MCU hang → IWDG reset → contactors open on boot (fail-safe init)
  - IWDG reset event logged to NVM

### P1-03: Gas Detection Interface

- **Description**: Zero gas detection integration. DNVGL-RU-SHIP Pt.6 Ch.2 Sec.1 §1.4 mandatory requirement. H₂/CO detection provides 30-120s earlier warning than temperature sensors.
- **Flagged by**: Catherine (CRITICAL), Mikael (HIGH), Henrik (SHOWSTOPPER), Priya (HIGH)
- **Effort**: 1-2 weeks (firmware + hardware integration)
- **Dependencies**: Hardware — 2 gas sensors + wiring; GPIO or CAN interface
- **Acceptance criteria**:
  - BMS input for gas detector low/high alarm (CAN or discrete GPIO)
  - Low alarm → increase ventilation command + warning on CAN
  - High alarm → open contactors + fault latch + emergency ventilation command
  - Gas alarm → system shutdown within 2s (demonstrable by test)

### P1-04: Ventilation Monitoring Interface

- **Description**: No ventilation status input. Battery room ventilation failure → hydrogen accumulation → explosion risk.
- **Flagged by**: Henrik (SHOWSTOPPER), Priya (MEDIUM)
- **Effort**: 3-5 days
- **Dependencies**: P1-03 (shared interface design)
- **Acceptance criteria**:
  - BMS input for ventilation status (running/failed)
  - Ventilation failure → alarm + load reduction + inhibit contactor closure
  - Ventilation restored → normal operation resumes after configurable delay

### P1-05: Fire Detection/Suppression Interface

- **Description**: No fire system integration. BMS must provide thermal status to ship fire panel and respond to suppression activation.
- **Flagged by**: Henrik (MAJOR), Priya (CRITICAL)
- **Effort**: 3-5 days
- **Dependencies**: P0-04 (dT/dt provides thermal runaway warning signal)
- **Acceptance criteria**:
  - BMS thermal fault output relay/CAN message to fire panel
  - Suppression activation input → BMS disconnect + post-incident interlock
  - Post-thermal-fault: manual-only reset (not 60s timer) requiring operator confirmation
  - dT/dt alarm → fire panel warning distinct from OT fault

### P1-06: Insulation Monitoring Interface

- **Description**: 1000Vdc system with zero ground fault detection. Class rules require continuous insulation monitoring.
- **Flagged by**: Dave (CRITICAL), Catherine (HIGH), Mikael (CRITICAL), Henrik (MAJOR)
- **Effort**: 1-2 weeks
- **Dependencies**: Hardware — IMD device (Bender ISOMETER or equivalent)
- **Acceptance criteria**:
  - HAL interface for IMD alarm input (relay contact or Modbus)
  - Insulation alarm → pack disconnect (non-optional)
  - Insulation resistance trend logging in NVM
  - Configurable alarm threshold per IEC 61557-8

### P1-07: Software Verification Foundation

- **Description**: Every file carries "SIMULATION DISCLAIMER." No SRS, no MISRA C analysis, no test coverage evidence. Cannot pass DNVGL-CP-0418.
- **Flagged by**: Henrik (SHOWSTOPPER)
- **Effort**: 2-3 months (ongoing, but foundation must start here)
- **Dependencies**: All Phase 0 fixes (code must stabilize before verification)
- **Acceptance criteria**:
  - Safety Requirements Specification document mapping firmware functions to safety requirements
  - SIL determination (expected: SIL 2)
  - MISRA C:2012 compliance analysis initiated with static analysis tool
  - Unit test framework operational with initial coverage of protection functions
  - "SIMULATION DISCLAIMER" removed from all files upon completion

---

## Phase 2: Production Hardening (Security, Reliability, Maintainability)

### P2-01: CAN Message Authentication

- **Description**: Zero authentication on CAN bus. Any device can send EMS commands. CVSS 9.8.
- **Flagged by**: All 6 reviewers
- **Effort**: 2-3 weeks
- **Dependencies**: None (but coordinate with P2-02 for key management)
- **Acceptance criteria**:
  - AES-128-CMAC on all safety-critical CAN frames (0x200, 0x210)
  - Monotonic sequence counter (4 bytes) to detect replay
  - Pre-shared key per EMS-BMS pair, provisioned during commissioning
  - Frames with invalid MAC or out-of-sequence counter rejected and logged
  - Heartbeat requires authenticated counter (defeats Ghost EMS attack)

### P2-02: Secure Boot and Firmware Signing

- **Description**: No bootloader, no signature verification, no rollback protection. Field updates brick system. Attacker with SWD access can replace firmware.
- **Flagged by**: Dave (CRITICAL), Mikael (MEDIUM), Yara (HIGH)
- **Effort**: 3-4 weeks
- **Dependencies**: None
- **Acceptance criteria**:
  - Dual-bank (A/B) flash architecture with header (version, CRC32, ECDSA-P256 signature)
  - Bootloader verifies image integrity before jump; falls back to known-good bank on failure
  - Power loss during write always leaves at least one valid image
  - Anti-rollback via monotonic OTP counter
  - Field update possible via CAN bus (authenticated image transfer)

### P2-03: Debug Interface Lockout

- **Description**: STM32F4 at RDP Level 0. $4 debug probe gives full firmware extraction/replacement.
- **Flagged by**: Yara (HIGH)
- **Effort**: 1 day (configuration) + process change for production
- **Dependencies**: P2-02 (secure boot must be working before locking debug)
- **Acceptance criteria**:
  - Production builds: RDP Level 2 (permanent, irreversible)
  - Development builds: RDP Level 1 minimum
  - SWD header depopulated on production PCBs
  - Tamper-evident seals on enclosure

### P2-04: NVM Integrity and Expanded Logging

- **Description**: 64-entry ring buffer with no CRC, no integrity protection, uptime-only timestamps. Inadequate for claims investigation or forensic analysis.
- **Flagged by**: Dave (HIGH), Catherine (HIGH), Henrik (MAJOR), Yara (MEDIUM)
- **Effort**: 1-2 weeks
- **Dependencies**: None
- **Acceptance criteria**:
  - HMAC-SHA256 per fault entry using device-unique key (STM32F4 UID)
  - Monotonic write counter (cannot be rolled back)
  - Expand to ≥1000 events
  - Add RTC timestamps (synced from EMS via authenticated CAN)
  - Add continuous black-box logging: cell voltages + temps + current at ≥1Hz, 2-hour circular buffer
  - CRC chain for tamper evidence
  - CAN broadcast flag when log >75% full

### P2-05: Protection Timer Preservation Across Fault Reset

- **Description**: `bms_protection_reset()` zeroes all leaky integrator timers. Timing attack: reset every 60s to prevent faults from ever latching.
- **Flagged by**: Yara (MEDIUM)
- **Effort**: 2-3 days
- **Dependencies**: None
- **Acceptance criteria**:
  - Fault reset zeroes fault flags but preserves integrator timer state
  - Maximum 3 resets per hour; after that, require physical intervention
  - Every reset logged to NVM with timestamp
  - After N consecutive faults of same type, require manual intervention (not CAN reset)

### P2-06: Input Validation on CAN Parameters

- **Description**: No range validation on current limits. Negative values bypass downward clamp. `EMS_CMD_NONE` passes validation.
- **Flagged by**: Yara (HIGH)
- **Effort**: 2-3 days
- **Dependencies**: None
- **Acceptance criteria**:
  - `charge_limit_ma` and `discharge_limit_ma` clamped to [0, BMS_MAX_*_MA]
  - Negative values rejected
  - Command type 0 (`EMS_CMD_NONE`) rejected or explicitly documented as heartbeat
  - Reserved bytes validated as zero

### P2-07: I2C Plausibility Checks

- **Description**: No integrity verification on BQ76952 reads. No cross-checks. Spoofed data accepted unconditionally.
- **Flagged by**: Dave (MEDIUM), Yara (CRITICAL), Priya (MEDIUM)
- **Effort**: 1-2 weeks
- **Dependencies**: None
- **Acceptance criteria**:
  - Stack voltage vs sum of cell voltages: divergence >2% → anomaly flag
  - Rate-of-change limit: |dV/dt| > 50mV per 10ms → anomaly flag
  - Cross-module temperature comparison: >20°C inter-module delta → anomaly flag
  - Sensor open/short detection via ADC range check
  - Read-back verify all configuration writes to BQ76952

### P2-08: CAN Bus Anomaly Detection

- **Description**: No rate limiting, no unexpected-ID detection, no bus error monitoring. CAN flood DoS trivial.
- **Flagged by**: Yara (MEDIUM)
- **Effort**: 1 week
- **Dependencies**: None
- **Acceptance criteria**:
  - Hardware CAN filter: accept only IDs 0x200 and 0x210 (rejects flood attack at hardware level)
  - Message rate monitoring: >10 frames/s on given ID → anomaly logged
  - CAN error counter (TEC/REC) monitoring and broadcast
  - Bus-off recovery with fault logging

### P2-09: EMS Watchdog in READY State

- **Description**: Watchdog only active in CONNECTED/CONNECTING. Pack sits in READY indefinitely with dead EMS.
- **Flagged by**: Dave (HIGH)
- **Effort**: 1 day
- **Dependencies**: None
- **Acceptance criteria**:
  - If no EMS heartbeat for 30 minutes in READY → transition to POWER_SAVE
  - Configurable timeout

### P2-10: Atomic NVM Writes

- **Description**: Three separate NVM writes in `bms_nvm_log_fault()`. Power loss between writes corrupts ring buffer.
- **Flagged by**: Dave (HIGH)
- **Effort**: 3-5 days
- **Dependencies**: P2-04 (combined with NVM overhaul)
- **Acceptance criteria**:
  - Write-then-commit pattern with per-event CRC
  - Two copies of head/count metadata with cross-validation
  - Partial write detected on reload (magic number + CRC)

---

## Phase 3: Competitive Parity (Features to Match Market Expectations)

### P3-01: Cooling System Health Monitoring

- **Description**: No fan tachometer input, no airflow sensor, no thermal resistance estimator. Fan failure invisible until OT thresholds breached.
- **Flagged by**: Catherine (HIGH), Mikael (HIGH), Priya (CRITICAL)
- **Effort**: 1-2 weeks (firmware + hardware: fan tach GPIO per fan)
- **Dependencies**: Hardware modification
- **Acceptance criteria**:
  - Fan tachometer GPIO input (or thermal resistance estimator: expected vs actual ΔT at known power)
  - Detect fan failure within 60s
  - Detect 50% cooling degradation within 5 minutes
  - Progressive derating on cooling degradation (not just fault at 65°C)

### P3-02: Per-Module Thermal Anomaly Detection

- **Description**: Firmware aggregates to pack-level min/max, losing per-module thermal information. A module running 10°C hotter than neighbors indicates degradation or cooling obstruction.
- **Flagged by**: Priya (HIGH)
- **Effort**: 2-3 days (~40 lines of C code)
- **Dependencies**: None
- **Acceptance criteria**:
  - Compare each module's temperature against pack average
  - Flag outliers >5°C above mean at steady-state conditions
  - Track module-level ΔT trends over hours/days

### P3-03: Stack-vs-Cells Cross-Check

- **Description**: `stack_mv` is read and stored but never compared against sum of cell voltages. Standard BMS practice for detecting sense line failures.
- **Flagged by**: Dave (HIGH)
- **Effort**: 1-2 days
- **Dependencies**: None
- **Acceptance criteria**:
  - After reading module: |sum(cell_mv) - stack_mv| > 100mV → suspect flag
  - 3+ consecutive scans with delta → equivalent of comm_loss for that module
  - Discrepancy logged (leading indicator of connector degradation)

### P3-04: Human-Readable Fault Messages

- **Description**: All `BMS_LOG()` compiles to `((void)0)` on target. Only persistent record is 8-byte NVM events with numeric codes. No fault descriptions, no recommended actions, no module identification.
- **Flagged by**: Dave (HIGH)
- **Effort**: 3-5 days
- **Dependencies**: None
- **Acceptance criteria**:
  - Const fault description lookup table compiled into target firmware
  - CAN broadcast includes human-readable fault info (not just bit flags)
  - Module ID (not just cell index) in fault records
  - Recommended first action per fault type

### P3-05: CAN Bus Redundancy

- **Description**: Single CAN bus. Cable fault = complete loss of BMS-to-EMS communication. 5s unmanaged battery operation.
- **Flagged by**: Catherine (MEDIUM), Mikael (HIGH), Henrik (MEDIUM)
- **Effort**: 2-3 weeks (firmware + hardware: second CAN transceiver)
- **Dependencies**: Hardware modification
- **Acceptance criteria**:
  - Dual CAN bus with automatic failover
  - Bus health monitoring on both channels
  - Transparent failover: EMS sees no interruption

### P3-06: Contactor Health Monitoring

- **Description**: Post-hoc weld detection only (200ms window). No pre-emptive monitoring of coil current, contact resistance, or switching cycles.
- **Flagged by**: Catherine (HIGH), Dave (HIGH)
- **Effort**: 1-2 weeks
- **Dependencies**: Possible hardware (coil current sense)
- **Acceptance criteria**:
  - Track contactor switching cycle count
  - Extended weld detection window (500ms minimum) with multi-point sampling
  - Check for re-welding (current drops then rises)
  - Alert when cycle count approaches end-of-life

### P3-07: Per-Module Current Measurement

- **Description**: BQ76952 CC2 coulomb counter driver exists but is never called. Enables module-level diagnostics, internal short detection, per-module SoH.
- **Flagged by**: Mikael (HIGH)
- **Effort**: 3-5 days
- **Dependencies**: None (driver already exists)
- **Acceptance criteria**:
  - `bq76952_read_current()` called during module scan
  - Per-module current stored and available on CAN
  - Anomaly detection: module current deviating from pack average

### P3-08: OCV Temperature Correction for SoC

- **Description**: OCV table is isothermal. At -20°C, SoC error of 5-8% propagates into current limit calculations.
- **Flagged by**: Priya (MEDIUM)
- **Effort**: 2-3 days (~30 lines of code)
- **Dependencies**: None
- **Acceptance criteria**:
  - Temperature dimension added to OCV table (or linear correction ~0.3 mV/°C)
  - SoC error at temperature extremes reduced from 5-8% to <2%

---

## Phase 4: Differentiation (Best-in-Class Features)

### P4-01: Predictive Multi-Zone Thermal Model

- **Description**: 22-node thermal model (one per module) with inter-module conduction, non-uniform airflow, and online calibration. Enables predictive derating 30s before hard thresholds.
- **Flagged by**: Mikael (HIGH), Priya (MEDIUM)
- **Effort**: 2-4 weeks (~500 lines of C code + thermal characterization testing)
- **Dependencies**: P3-01 (cooling health monitoring), P3-02 (per-module anomaly detection)
- **Acceptance criteria**:
  - State estimator at 1 Hz using measured temps to calibrate model parameters
  - Estimated temperatures at unmeasured locations
  - Time-to-thermal-limit prediction
  - Proactive derating 30s before hard threshold

### P4-02: Active Balancing

- **Description**: Passive balancing at 50mA on 128Ah cells. 5% SoC delta takes 128 hours to correct. Active balancing recovers 2-5% usable capacity over pack lifetime.
- **Flagged by**: Mikael (MEDIUM), Catherine (LOW)
- **Effort**: Major — hardware redesign (switched-capacitor or transformer-based)
- **Dependencies**: Hardware modification
- **Acceptance criteria**:
  - Balance current ≥1A
  - 5% SoC delta corrected within 8 hours
  - Thermal impact of balancing characterized

### P4-03: Impedance-Based State of Health

- **Description**: No impedance tracking, no capacity fade monitoring. Coulomb counting drifts further over time. After 2-3 years, SoC accuracy degrades significantly.
- **Flagged by**: Catherine (MEDIUM), Mikael (LOW-MEDIUM)
- **Effort**: 2-4 weeks
- **Dependencies**: P3-07 (per-module current measurement for EIS excitation)
- **Acceptance criteria**:
  - Quarterly EIS measurement or online impedance estimation
  - Cell impedance trends tracked and anomalous cells flagged
  - Capacity fade model adjusting `BMS_NOMINAL_CAPACITY_MAH` over time
  - Protection thresholds adapt to aging (wider UV margin as capacity fades)

### P4-04: EtherCAT Communication Option

- **Description**: CAN 2.0B limits cell broadcast to 7.7s cycle. EtherCAT provides all 308 cells in a single 1ms frame.
- **Flagged by**: Mikael (MEDIUM)
- **Effort**: 4-6 weeks (new hardware interface + protocol stack)
- **Dependencies**: Hardware modification
- **Acceptance criteria**:
  - All 308 cell voltages + 66 temperatures in single frame at ≤1ms
  - Backward-compatible CAN interface retained
  - Standard EtherCAT diagnostics

### P4-05: Black Box / VDR Integration

- **Description**: No continuous data recording, no absolute timestamps, no tamper evidence for voyage data recording.
- **Flagged by**: Catherine (HIGH), Henrik (OBSERVATION)
- **Effort**: 2-3 weeks
- **Dependencies**: P2-04 (NVM overhaul), RTC implementation
- **Acceptance criteria**:
  - IEC 61996-compliant event recording
  - All cells + temps + current at ≥1Hz, 24-hour retention
  - Cryptographically signed records
  - Tamper-evident extraction interface

---

## Mask Improvement Plan

Based on self-assessments, sources to fetch for improved mask readiness in future reviews.

### Field Tech (Dave) — Current: 42%

| Priority | Source to Fetch | Expected Gain |
|----------|----------------|---------------|
| P0 | Extract NSIA Brim report PDF (already in sources/) | +5% |
| P0 | Extract NMA consultation letter PDF (already in sources/) | +3% |
| P1 | SAE/IEEE papers on EV contactor welding and bounce | +5% |
| P1 | Corvus Orca commissioning checklist (if obtainable) | +5% |
| P2 | CAN bus reliability data from automotive EMC studies | +3% |
| P2 | IEC 61557-8 / Bender IMD application notes | +3% |
| P3 | Norwegian ferry operator reports (retry in Norwegian) | +5% |

### Insurance Adjuster (Catherine) — Current: 35%

| Priority | Source to Fetch | Expected Gain |
|----------|----------------|---------------|
| P0 | DNVGL-RU-SHIP Pt.6 Ch.2 Sec.1 (free at rules.dnv.com) | +15% |
| P0 | Ytterøyningen investigation report (NSIA, public) | +10% |
| P1 | IUMI Facts & Figures 2023-2025 annual reports/presentations | +8% |
| P1 | Gard and Swedish Club loss prevention publications on batteries | +5% |
| P2 | DNV Battery Safety JIP summary findings | +5% |
| P2 | Sandia battery fire test reports | +5% |

### Competitor Engineer (Mikael) — Current: 68%

| Priority | Source to Fetch | Expected Gain |
|----------|----------------|---------------|
| P1 | EST-Floattech, Echandia, Siemens BlueDrive datasheets | +5% |
| P1 | PBES/Ytterøyningen NMA failure analysis | +8% |
| P2 | Competitor patent filings (EST-Floattech, Echandia) | +5% |
| P2 | IEEE papers on marine BMS architecture | +5% |
| P3 | BQ76952 vs ADBMS6830 vs MAX17853 comparison | +3% |

### Class Surveyor (Henrik) — Current: 45%

| Priority | Source to Fetch | Expected Gain |
|----------|----------------|---------------|
| P0 | DNVGL-RU-SHIP Pt.6 Ch.2 Sec.1 full text | +20% |
| P0 | DNVGL-CP-0418 full text | +10% |
| P1 | Verify IEC 62619 PDF completeness (15pp vs ~80pp) | +5% |
| P1 | IEC 61508 summary/checklist | +5% |
| P2 | MF Ytterøyningen NSIA investigation report | +5% |
| P2 | DNV-ST-0033 recommended practice | +5% |

### Pentester (Yara) — Current: 42%

| Priority | Source to Fetch | Expected Gain |
|----------|----------------|---------------|
| P1 | NIST SP 800-82 Rev 3 (free PDF) | +8% |
| P1 | Miller & Valasek CAN bus attack papers | +5% |
| P1 | CISA ICS advisories for BMS/UPS systems | +5% |
| P2 | OWASP Firmware Security Testing Methodology | +5% |
| P2 | STM32F4 security features documentation | +5% |
| P3 | Plymouth maritime CAN thesis — extract key findings | +3% |

### Thermal Engineer (Priya) — Current: 62%

| Priority | Source to Fetch | Expected Gain |
|----------|----------------|---------------|
| P1 | NMC 622 ARC/abuse test papers (Dahn group, etc.) | +8% |
| P1 | ISO 8861 marine ambient thermal data | +5% |
| P2 | DNV Battery Safety JIP summary materials | +8% |
| P2 | DOE/OSTI thermal barrier test reports | +5% |
| P3 | Gas generation rate data for NMC 622 (for ventilation sizing) | +3% |

---

## Re-review Triggers

The following changes would warrant running the full Mockingbird panel again:

### Mandatory Re-review

1. **Phase 0 + Phase 1 completion** — After all critical safety and classification readiness items are implemented, run all 6 masks to verify fixes and identify any new issues introduced
2. **Hardware schematic available** — Would resolve the top-tier unknown unknowns (separate safety controller, BQ76952 FET wiring, fail-safe power design). Could fundamentally change Henrik's and Priya's assessments
3. **Thermal runaway propagation test data available** (UL 9540A results) — Would allow Priya and Catherine to provide quantitative risk assessment instead of worst-case assumptions
4. **CAN authentication implemented** — Yara should re-assess the complete security posture post-remediation

### Recommended Re-review

5. **Phase 2 completion** — Security hardening changes the attack surface; pentester re-review validates
6. **Change of cell chemistry or supplier** — Different thermal characteristics invalidate Priya's analysis
7. **Multi-pack array firmware available** — Catherine's cascade scenario and Mikael's coordination protocol concerns need array-level code
8. **RTOS integration complete** — Dave's and Yara's unknown unknowns about task timing and stack overflow need real RTOS code
9. **Classification survey feedback** — If a real DNV surveyor provides findings, compare against Henrik's predictions

### Periodic Re-review

10. **Annually** — Regulatory landscape evolves (IMO SDC 12, IACS UR E26/E27, IEC 63462-1). Masks should be updated with new standards and the firmware re-assessed against current requirements.

---

*Implementation plan completed 2026-03-03. Estimated total effort: Phase 0 (1-2 weeks), Phase 1 (3-4 months), Phase 2 (2-3 months), Phase 3 (2-3 months), Phase 4 (6+ months). Phases 0-2 are sequential prerequisites. Phases 3-4 can overlap with Phase 2.*
