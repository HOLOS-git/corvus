# V2 DELTA REVIEW — DNV Classification Survey Assessment

**Surveyor**: Henrik Nordahl, DNV GL — Senior Surveyor, Electrical Systems  
**Review Date**: 2026-03-03  
**Subject**: Corvus Orca ESS Pack Controller Firmware — Street Smart Edition  
**Scope**: Delta assessment against V1 findings (MOCKINGBIRD-CLASS-001 Rev A)  
**Document Ref**: MOCKINGBIRD-CLASS-002 Rev A  

---

## 1. Revised Verdict

### **CONDITIONAL — Upgraded from FAIL to CONDITIONAL PASS (with remaining conditions)**

The V2 firmware addresses the three critical areas that drove my V1 rejection. The architectural changes are substantive, not cosmetic. I would now recommend proceeding to witness testing, subject to the remaining conditions documented below.

This is a significant improvement. The development team clearly read and understood the findings — every change traces back to a specific hold point with reviewer attribution. That traceability is itself a positive indicator of process maturity.

---

## 2. V1 FAIL Disposition — 6 Original FAILs

### FAIL #2.9: Protection system independence — ❌→ ⚠️ PARTIAL (upgraded)

**V1 finding**: Both protection layers execute on same MCU with no independent trip path.

**V2 changes**:
- `bq76952_configure_hw_protection()` now **writes ENABLE_PROT_A/B/C** registers during init, enabling autonomous OV/UV/OT/SC protection with FET gate drive. This was the single most important change. The BQ76952 will now trip its own FET outputs independently of the STM32F4.
- **Read-back verification** on all config writes (P2-07). Good practice.
- **IWDG configured** at ≤100ms timeout (`hal_iwdg_init`), fed from protection loop. MCU hang → reset → contactors open on boot.
- IWDG reset detection logged to NVM for incident investigation.

**What remains open**: I still cannot verify from firmware alone that the BQ76952 CHG/DSG FET outputs are **physically wired** to an independent contactor trip path. The firmware correctly configures the AFE for autonomous protection, but if the FET outputs are not connected to contactor drive hardware, the protection dies at the IC pin. **This requires schematic review.**

However — the IWDG now ensures MCU hang → reset → contactors open (via `bms_contactor_init`). Combined with BQ76952 autonomous FET control, this is a credible two-layer architecture **if the hardware supports it**.

**New status**: Upgraded from SHOWSTOPPER to MAJOR. Firmware side is addressed. Hardware schematic required to close.

---

### FAIL #2.13: Gas detection interface — ❌→ ✅ RESOLVED

**V1 finding**: No evidence of gas detection anywhere in firmware.

**V2 changes** (`bms_safety_io.c`):
- `GPIO_GAS_ALARM_LOW` / `GPIO_GAS_ALARM_HIGH` discrete inputs defined
- Low alarm → warning + increase ventilation command (`GPIO_VENT_CMD`)
- High alarm → `SAFETY_IO_SHUTDOWN` → `fault_latched` + emergency ventilation
- `bms_safety_io_emergency_shutdown()` checked by state machine → immediate FAULT
- `bms_safety_io_inhibit_close()` prevents contactor closure during gas alarm
- Safety I/O status transmitted on CAN ID 0x150
- Response within 100ms polling period (well within 2s requirement)

**Assessment**: Meets DNVGL-RU-SHIP Pt.6 Ch.2 Sec.1 §1.4 intent for BMS integration with gas detection. The actual gas detection system specification (sensor type, placement, calibration) is outside BMS firmware scope.

**Status**: ✅ RESOLVED

---

### FAIL #2.14: Ventilation monitoring interface — ❌→ ✅ RESOLVED

**V1 finding**: No ventilation monitoring or control.

**V2 changes** (`bms_safety_io.c`):
- `GPIO_VENT_STATUS` input monitors ventilation running/failed
- Vent failure → `SAFETY_IO_ALARM` → warning + contactor close inhibited
- 30s restoration delay before clearing alarm (configurable `BMS_VENT_RESTORE_DELAY_MS`)
- `GPIO_VENT_CMD` output for emergency ventilation increase on gas or thermal alarm
- Integration with state machine: `bms_safety_io_inhibit_close()` blocks READY→CONNECTING transition on vent failure

**Assessment**: All five integration points I specified in V1 §5 are addressed:
1. ✅ Input: gas detector status
2. ✅ Input: ventilation status
3. ✅ Output: ventilation increase command
4. ✅ Logic: inhibit contactor closure on vent failure
5. ✅ Logic: force contactor open on high gas alarm

**Status**: ✅ RESOLVED

---

### FAIL #2.15: Fire detection/suppression interface — ❌→ ✅ RESOLVED

**V1 finding**: No fire detection or suppression interface.

**V2 changes**:
- `GPIO_FIRE_DETECT` input → `SAFETY_IO_SHUTDOWN` → disconnect + fault latch
- `GPIO_FIRE_SUPPRESS_IN` input → post-incident interlock with **manual-only reset** (not 60s timer)
- `GPIO_FIRE_RELAY_OUT` output to fire panel, also driven by dT/dt alarm
- `bms_protection_can_reset()` explicitly **denies reset** on fire/suppression faults — CAN reset command is rejected
- New `bms_thermal.c`: dT/dt rate-of-rise detection at 1°C/min threshold with 30s sustain, load-correlation check to exclude I²R heating

**Assessment**: The fire interface is complete. The dT/dt detection is a particularly good addition — it provides the early warning signal I specified in HC-04. The load-correlation heuristic (50A threshold) is a reasonable first cut, though it should be validated against real thermal runaway test data.

**Status**: ✅ RESOLVED

---

### FAIL #2.18: Insulation monitoring — ❌→ ✅ RESOLVED

**V1 finding**: No insulation resistance measurement.

**V2 changes** (`bms_safety_io.c`):
- `GPIO_IMD_ALARM` discrete input from external IMD device
- `ADC_IMD_RESISTANCE` analog input for resistance measurement (IEC 61557-8)
- Configurable alarm threshold (`BMS_IMD_ALARM_THRESHOLD_KOHM = 100 kΩ`)
- IMD alarm → `SAFETY_IO_SHUTDOWN` → pack disconnect + fault latch
- **Periodic resistance trend logging to NVM** every 60s — excellent for degradation trending during periodic survey

**Assessment**: Proper approach — external IMD device with BMS integration. The trend logging is beyond the minimum requirement and demonstrates good engineering judgment. This data would be valuable during annual survey for identifying insulation degradation trends.

**Status**: ✅ RESOLVED

---

### FAIL #2.19: Software verification — ❌→ ❌ REMAINS OPEN

**V1 finding**: Every file carries "SIMULATION DISCLAIMER: not production code."

**V2 status**: The disclaimer text appears to have been removed or replaced with reviewer-attribution headers. However, the fundamental requirements remain unmet:
- No Safety Requirements Specification
- No SIL determination
- No MISRA C analysis
- No static analysis results
- No MC/DC coverage evidence
- No independent verification report

**Assessment**: This is expected — the firmware is clearly still in development. The code quality has improved (named constants in `bms_config.h`, `_Static_assert` validation, comprehensive fault typing), but these are engineering improvements, not verification evidence.

**Status**: ❌ REMAINS OPEN — expected to be addressed in production phase per DNVGL-CP-0418

---

## 3. V1 Hold Point Disposition — 5 Original Hold Points

### HC-01: Hardware safety layer independence — ⚠️ PARTIALLY RESOLVED

| Sub-requirement | V1 Status | V2 Status |
|-----------------|-----------|-----------|
| Configure BQ76952 autonomous protection | ❌ | ✅ `bq76952_configure_hw_protection()` writes ENABLE_PROT_A/B/C with read-back verify |
| Hardware schematic showing independent trip path | ❌ | ❌ Cannot assess from firmware |
| Enable MCU hardware watchdog (IWDG) | ❌ | ✅ `hal_iwdg_init(100ms)`, fed from protection loop |
| MCU hard fault → contactors open within 1s | ❌ | ✅ IWDG 100ms + reset + `contactor_init` opens all |

**Remaining**: Schematic review required. 3 of 4 sub-requirements met in firmware.

---

### HC-02: Gas detection interface — ✅ RESOLVED

All requirements met. See FAIL #2.13 above.

---

### HC-03: Ventilation monitoring interface — ✅ RESOLVED

All requirements met. See FAIL #2.14 above.

---

### HC-04: Fire detection interface — ✅ RESOLVED

All requirements met including dT/dt rate-of-rise detection. See FAIL #2.15 above.

---

### HC-05: Software verification — ❌ REMAINS OPEN

Expected to be addressed in production phase. Not a firmware architecture issue.

---

## 4. V1 Major Findings Disposition

### MF-01: CAN bus command authentication — ⚠️ PARTIALLY ADDRESSED

V2 implements a sequence counter framework (`bms_can_auth_verify/sign`) with `BMS_CAN_AUTH_ENABLED` compile flag. Currently disabled (set to 0). The stub architecture is correct — monotonic 16-bit sequence counter with reject on mismatch.

However: sequence counters alone do not provide authentication. A replay attack can succeed by incrementing the counter. Full CMAC (as noted in the code as "Phase 2") would be required. The code comments acknowledge this explicitly.

**Remaining**: CMAC or equivalent message authentication. Sequence counter is a good first step for anti-replay but insufficient for authentication.

---

### MF-02: Insulation monitoring — ✅ RESOLVED

See FAIL #2.18 above.

---

### MF-03: NVM fault log integrity — ❌ REMAINS OPEN

The NVM module has been expanded with new fault types (21 total, up from ~6), IWDG reset logging, and IMD trend logging. However:
- **Still no CRC-32 per fault event entry**
- **Still no monotonic write counter** for tamper/rollback detection
- **No double-buffered write** or journaling for power-loss protection

The `bms_nvm_fault_event_t` structure remains: `{timestamp, fault_type, cell_index, value}` — 8 bytes with no integrity field.

**Remaining**: Add per-event CRC and write counter. This is survey evidence preservation — without integrity protection, fault logs may not be admissible for incident investigation.

---

### MF-04: Overcurrent protection completeness — ✅ RESOLVED

V2 `bms_protection_run()` computes OC charge limit via `bms_current_limit_compute()` at all temperatures, not just sub-zero. The charge current is checked against the temperature/SoC-dependent limit with the standard 5s leaky integrator delay. Additionally, the sub-zero charge fault (P0-05) provides a hard 0A limit below freezing with dedicated fault type and NVM logging.

**Status**: ✅ RESOLVED

---

## 5. New Positive Observations

### O-01: Configuration centralization (`bms_config.h`)

All thresholds, timing constants, and tuning parameters are now in a single header with named constants, grouped by subsystem, with reviewer attribution and `_Static_assert` compile-time validation. This is exactly what I want to see for type approval — every parameter is traceable, auditable, and independently verifiable. No magic numbers buried in source files.

### O-02: Fault taxonomy expansion

Fault flags expanded from ~12 to 24 distinct types in a 32-bit bitfield with `_Static_assert` size check. Each new fault type (sensor fault, dT/dt, sub-zero charge, gas, vent, fire, IMD, plausibility, IWDG) has a corresponding NVM fault type for logging. This is the level of fault discrimination needed for incident investigation.

### O-03: Safety I/O architecture

The `bms_safety_io` module is cleanly separated with clear integration points: `inhibit_close()`, `emergency_shutdown()`, CAN encoding. The state machine checks these at every transition. This is the right pattern — safety I/O as a separate concern that gates state machine decisions.

### O-04: Thermal rate-of-rise detection

The `bms_thermal.c` module implements dT/dt monitoring with a 30-sample moving average, 1°C/min threshold, 30s sustain requirement, and load-correlation check. This provides the early warning capability I specified in HC-04. The load-correlation check (50A heuristic) is a thoughtful addition that reduces false alarms from normal I²R heating.

### O-05: Protection reset hardening (Yara's timing attack)

`bms_protection_reset()` now preserves integrator timers (only clears fault flags), enforces 3-reset-per-hour rate limit, and logs every reset to NVM. This closes the timing attack where repeated resets prevented faults from ever latching.

---

## 6. Remaining Conditions of Class

### Hold Points (Must Resolve Before Proceeding to Witness Test)

**HC-01 (revised): Provide hardware schematic demonstrating independent contactor trip path**
- BQ76952 FET outputs → contactor drive hardware → contactors open without MCU involvement
- Firmware side is now addressed; hardware verification outstanding
- *Rule reference*: DNVGL-RU-SHIP Pt.6 Ch.2 Sec.1 §1.3

**HC-05 (unchanged): Software verification per DNVGL-CP-0418**
- SRS, SIL determination, MISRA C, static analysis, MC/DC coverage
- Expected at production phase — not blocking architecture review
- *Rule reference*: DNVGL-CP-0418, IEC 61508-3

### Major Findings (Must Resolve Before Final Survey)

**MF-01 (revised): CAN bus message authentication**
- Sequence counter stub present; CMAC or equivalent required for production
- *Status*: Acknowledged by development team, deferred to Phase 2

**MF-03 (unchanged): NVM fault log integrity**
- CRC-32 per event, monotonic write counter, power-loss protection
- *Status*: Not addressed in V2

### Observations (No Action Required for Classification)

All V1 observations (OB-01 through OB-04) remain valid and unchanged.

---

## 7. Scorecard Summary

| V1 Finding | V1 Status | V2 Status | Change |
|------------|-----------|-----------|--------|
| **6 FAILs** | | | |
| 2.9 Protection independence | ❌ FAIL | ⚠️ PARTIAL | BQ76952 configured + IWDG; schematic needed |
| 2.13 Gas detection | ❌ FAIL | ✅ PASS | Full implementation |
| 2.14 Ventilation | ❌ FAIL | ✅ PASS | Full implementation |
| 2.15 Fire detection | ❌ FAIL | ✅ PASS | Full implementation + dT/dt |
| 2.18 Insulation monitoring | ❌ FAIL | ✅ PASS | Full implementation + trend logging |
| 2.19 Software verification | ❌ FAIL | ❌ FAIL | Expected — production phase |
| **5 Hold Points** | | | |
| HC-01 HW safety independence | ❌ | ⚠️ 3/4 | Schematic outstanding |
| HC-02 Gas detection | ❌ | ✅ | Resolved |
| HC-03 Ventilation | ❌ | ✅ | Resolved |
| HC-04 Fire detection | ❌ | ✅ | Resolved |
| HC-05 Software verification | ❌ | ❌ | Expected — production phase |
| **4 Major Findings** | | | |
| MF-01 CAN authentication | ❌ | ⚠️ | Stub present, CMAC deferred |
| MF-02 Insulation monitoring | ❌ | ✅ | Resolved |
| MF-03 NVM integrity | ❌ | ❌ | Not addressed |
| MF-04 OC charge protection | ❌ | ✅ | Resolved |

**V1 total**: 10 PASS, 3 PARTIAL, 6 FAIL → **V2 total**: 14 PASS, 2 PARTIAL, 3 FAIL

---

## 8. Would I Change My Verdict?

**Yes.** V1 was "Cannot recommend for type approval or newbuild survey in current state." V2 is "Recommend proceeding to witness testing, subject to schematic review and remaining conditions."

The three architectural showstoppers (no independent protection, no safety I/O integration, no gas/vent/fire/IMD) have been addressed at the firmware level. The remaining open items (schematic verification, NVM integrity, CAN authentication, software verification) are either hardware documentation requirements or production-phase activities.

If the hardware schematic confirms an independent BQ76952 FET → contactor trip path, I would upgrade HC-01 to RESOLVED and classify the system as ready for integration testing on a test bench. The software verification (HC-05) is a gate for production release, not for development testing.

This is the most responsive rewrite to classification findings I have seen in my career. The traceability from finding to code change to reviewer attribution is exemplary. If the production verification process shows the same discipline, this system will pass survey.

---

*Henrik Nordahl*  
*DNV GL — Senior Surveyor, Electrical Systems*  
*18 years classification survey experience*  

*This delta review is based on firmware source code inspection only, comparing V2 (Street Smart Edition) against V1 findings. Hardware schematic review and witness testing remain outstanding. Findings are subject to revision upon receipt of additional documentation.*
