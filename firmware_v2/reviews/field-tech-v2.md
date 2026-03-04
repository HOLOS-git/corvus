# Field Tech Delta Review — Dave's V2 Assessment

**Reviewer:** Dave, Independent Marine Electrical Contractor  
**Subject:** Corvus Orca ESS BMS Firmware — Street Smart Edition  
**Date:** 2026-03-03  
**Classification:** DELTA REVIEW — Comparison against V1 findings

---

## 1. Executive Summary

### **VERDICT: CONDITIONAL YES — Approve for bench testing and supervised sea trials.**

I'll be honest. I didn't expect to change my mind. But whoever did this rewrite actually *read* my findings and understood *why* they mattered, not just what I wrote. Every one of my five critical findings has been addressed. Some of them have been addressed well. A few have introduced new wrinkles I want to talk about.

This is no longer a bench demo. It's approaching vessel-ready code. I'd sign off on commissioning with conditions — see Section 5.

---

## 2. Critical Findings — Status

### CRITICAL-01: Temperature Sensor Failure Reads as Normal ✅ FIXED

**What changed:** `bq76952_read_temperature()` now returns `BMS_TEMP_SENSOR_SENTINEL` (`INT16_MIN`) on I2C failure instead of 0. The monitor module (`bms_monitor.c`) has a full sensor fault pipeline:

- Sentinel detection (immediate)
- Plausibility bounds check (-40°C to 120°C)
- Exact-zero suspicion (3 consecutive scans of 0.0°C → fault)
- Adjacent sensor cross-check (>20°C delta = warning)
- 3 consecutive bad scans → `sensor_fault = true` → `fault_latched = true`
- Faulted sensors excluded from aggregation

**My assessment:** This is solid. The exact-zero check is smart — that's exactly what a corroded NTC connection reads. The adjacent sensor cross-check catches the scenario I described where one connector degrades while others are fine. The 3-scan consecutive count prevents EMI transients from false-tripping.

**One concern:** `last_valid_deci_c` is used for aggregation when a sensor is suspect but not yet faulted. If the last valid reading was 60°C and the sensor dies, you're running with a stale 60°C reading for 3 scans (30ms at 10ms period). That's probably fine for 30ms, but I'd want to see this validated on the bench.

---

### CRITICAL-02: Communication Loss Does Not Latch Fault ✅ FIXED

**What changed:** `bms_monitor_read_module()` now tracks `i2c_fail_count` per module. After 3 consecutive failures, `fault_latched = true` is set. First failure triggers `hal_i2c_bus_recovery()`. Clean separation: one retry with bus recovery, then two more chances, then hard fault.

**My assessment:** Exactly what I asked for. The bus recovery on first failure is a nice touch — I've seen SCL/SDA stuck conditions clear with a clock toggle many times. Three consecutive failures before latching is the right balance between false trips from EMI and catching real failures.

---

### CRITICAL-03: No Firmware Update Mechanism ⚠️ PARTIALLY ADDRESSED

**What changed:** The README mentions "Secure bootloader with firmware signing" as a key improvement. The IWDG reset detection and NVM logging are in place (`hal_iwdg_was_reset()`, `NVM_FAULT_IWDG`). The HAL stubs for STM32F4 now include IWDG init/feed/reset detection.

**What's missing:** There is still **no bootloader code** in this codebase. No dual-bank flash, no image header validation, no CRC check, no CAN-bus firmware update protocol. The README claims it, but the code doesn't have it.

**My assessment:** This is better — the IWDG means a bricked MCU at least resets instead of hanging with contactors closed. But the core problem remains: you still can't update this firmware in the field without JTAG. The README claiming "Secure bootloader with firmware signing" when there's no bootloader code is concerning. Don't tell the commissioning engineer something exists when it doesn't.

**Severity: Downgraded from CRITICAL to HIGH.** The IWDG makes a hang recoverable, which removes the "bricked with contactors closed" scenario. But field update capability is still needed before production deployment.

---

### CRITICAL-04: No Ground Fault / Insulation Monitoring ✅ FIXED

**What changed:** New `bms_safety_io.c` module with full IMD integration:

- HAL interface for IMD alarm GPIO (`GPIO_IMD_ALARM`)
- ADC channel for insulation resistance analog output (`ADC_IMD_RESISTANCE`)
- Configurable alarm threshold (100 kΩ per IEC 61557-8)
- IMD alarm → `fault_latched = true` → pack disconnect
- Periodic resistance trend logging to NVM (every 60s)
- CAN broadcast of safety I/O state (0x150)

**My assessment:** This is exactly what I wanted to see. The trend logging is a bonus — insulation degradation is gradual, and having a resistance history makes preventive maintenance possible. The alarm threshold of 100 kΩ is correct for a 1000V system per IEC 61557-8.

The implementation correctly treats IMD alarm as non-negotiable: `SAFETY_IO_SHUTDOWN` level, `fault_latched`, pack disconnect. No wiggle room. Good.

---

### CRITICAL-05: Pre-charge Uses Pack Voltage Instead of Bus Voltage ✅ FIXED

**What changed:** `bms_contactor.c` now:

1. Reads actual bus voltage via `hal_adc_read(ADC_BUS_VOLTAGE)`
2. Pre-charge target = 95% of bus voltage
3. Voltage match check: `|pack_voltage - bus_voltage| < BMS_VOLTAGE_MATCH_MV` before main contactor closure
4. If voltage match fails after timeout, aborts with specific error log

State machine passes `pack->bus_voltage_mv` (populated by monitor from ADC) to `bms_contactor_request_close()`.

**My assessment:** The logic is right. The voltage match check before closing the main contactor is the key safety gate — this prevents the 30V-differential-contactor-welding scenario I described.

**One concern:** The ADC scaling in `bms_contactor.c` line `current_bus_mv = current_bus_mv * 1000U / 4U` is marked "placeholder scaling." The monitor uses a different scaling: `pack->bus_voltage_mv = hal_adc_read(ADC_BUS_VOLTAGE) * 1000U / 4095U`. These are inconsistent — the contactor code divides by 4, the monitor divides by 4095. One of them is wrong. On real hardware with a real voltage divider, this needs to be a single calibration constant, not two different magic numbers.

---

## 3. High-Priority Findings — Status

### HIGH-01: EMS Watchdog Only Active in CONNECTED/CONNECTING ✅ FIXED

`bms_state.c` now checks EMS heartbeat in READY state. 30 minutes without EMS → transitions to POWER_SAVE (`BMS_EMS_READY_TIMEOUT_MS = 1800000`). Timer starts when entering READY.

### HIGH-02: Contactor Weld Detection Window Too Short ✅ FIXED

`BMS_WELD_DETECT_MS` increased from 200ms to 500ms. Comments reference Catherine and my feedback. Still a single threshold check at timeout — would have preferred multi-sample with re-weld detection, but 500ms is a reasonable improvement.

### HIGH-03: Fault Messages Are Not Human-Readable on Target ⚠️ PARTIALLY FIXED

NVM fault types are now an enum with descriptive names (`NVM_FAULT_OV`, `NVM_FAULT_SENSOR`, `NVM_FAULT_IMD`, etc. — 21 types). Fault events still store the same 8-byte structure (timestamp, type, cell, value). `BMS_LOG()` calls throughout the code have excellent context — module numbers, values, specific P-ticket references.

**BUT:** `BMS_LOG()` still compiles to `((void)0)` on the target. Those beautiful log messages vanish on the actual hardware. The NVM stores a type number, not a string. A technician at 2am still needs a lookup table to decode fault_type=12 → "IMD alarm."

There's no CAN message that carries a human-readable fault description. The fault byte in the status frame is still a packed bitfield.

**My assessment:** The infrastructure is better (21 typed fault events vs. 3), but the last-mile problem remains: the person debugging at 2am still can't read the fault without a manual. Compile the fault description table into the target and expose it over CAN/Modbus.

### HIGH-04: NVM Writes Are Not Atomic ⚠️ NOT FIXED

`bms_nvm_log_fault()` still performs three sequential NVM writes with no atomicity guarantee. No CRC on events, no double-buffered metadata, no write-then-commit pattern. Same code as the original.

### HIGH-05: No Undertemperature Charging Fault ✅ FIXED

New sub-zero charging protection in `bms_protection.c`:
- `BMS_SUBZERO_CHARGE_MARGIN_MA = 0` (zero margin, not 5A)
- `BMS_SUBZERO_TEMP_THRESHOLD_DC = 0` (0°C threshold)
- 5 seconds sustained → `fault_latched`
- `_Static_assert(BMS_SUBZERO_CHARGE_MARGIN_MA == 0, "P0-05: 0A margin below freezing")` — compile-time enforcement

This is exactly right. The static assert preventing someone from accidentally changing the margin is a nice defensive touch.

### HIGH-06: No Stack-vs-Cells Cross-Check ✅ FIXED

`bms_monitor_read_module()` now compares `sum(cell_mv)` vs `stack_mv` with a 2% threshold. Sets `plausibility` fault flag on mismatch. Also added dV/dt rate-of-change check (50mV per scan max) and inter-module temperature comparison (20°C max delta).

---

## 4. New Issues Introduced

### NEW-01: ADC Bus Voltage Scaling Inconsistency (HIGH)

As noted in CRITICAL-05 above, there are two different ADC-to-voltage conversions for the bus voltage:
- `bms_contactor.c`: `current_bus_mv * 1000U / 4U` (= ×250)
- `bms_monitor.c`: `hal_adc_read() * 1000U / 4095U` (= ×0.244)

These can't both be right. On a 12-bit ADC reading a 1000V bus through a voltage divider, only one scaling factor is correct. Getting bus voltage wrong defeats the entire pre-charge fix.

### NEW-02: Thermal History Buffer Size (MEDIUM)

`bms_thermal_state_t` contains `int16_t temp_history[66][30]` — that's 3,960 bytes just for the history buffer, plus 66×4 bytes for alarm timers. Total thermal state is ~4.3KB. On an STM32F4 with 192KB SRAM, that's 2.2% of RAM for one subsystem. Not fatal, but worth checking the total RAM budget with all the other expanded structures.

The pack data structure itself is now significantly larger with `prev_cell_mv[14]` per module, `sensor_fault[3]` per module, etc. Someone should do a RAM audit.

### NEW-03: Safety I/O Assumes Digital GPIO for Gas Detection (LOW)

`bms_safety_io.c` reads gas alarm via `hal_gpio_read(GPIO_GAS_ALARM_LOW/HIGH)`. There's also `ADC_GAS_ANALOG` defined in the HAL but never used in the safety I/O code. Most marine gas detectors I've installed provide both a relay contact AND an analog 4-20mA output. The analog output gives you concentration trending (like the IMD resistance trending). The code only uses the relay contacts. Not wrong, but leaving capability on the table.

### NEW-04: No Critical Sections Around Shared Pack Data (MEDIUM)

In the RTOS task configuration (`bms_tasks.c`), multiple tasks access `g_pack` without any mutex or critical section:
- `task_monitor` writes cell voltages and temps (priority 4)
- `task_protection` reads cell voltages and temps (priority 5)
- `task_state` reads/writes mode, fault flags (priority 2)
- `task_safety_io` writes fault flags (priority 2)

Higher-priority tasks preempt lower ones. If `task_protection` (pri 5) preempts `task_monitor` (pri 4) mid-write, it reads a partially-updated pack structure. This could cause a false OV trip (half the cells updated to new values, half still old) or miss a real fault.

The bare-metal main loop doesn't have this problem (cooperative scheduling), but the RTOS path does. The HAL provides `hal_critical_enter/exit` but they're never used around pack data access.

### NEW-05: `bms_can_decode_ems_command` Reserved Byte Check Conflicts with Auth (LOW)

The CAN decoder rejects frames where `data[5..7]` are non-zero (reserved bytes). But the auth module writes sequence counters into `data[6..7]`. If `BMS_CAN_AUTH_ENABLED` is set to 1, the sequence counter in bytes 6-7 will be non-zero, and the decoder will reject every authenticated command frame.

These two features are currently not both enabled (auth is 0), but when someone enables auth, every EMS command will be rejected.

---

## 5. Commissioning Conditions

I'd approve commissioning with these conditions:

1. **Fix the ADC bus voltage scaling** (NEW-01). Both paths must use the same calibrated scaling factor validated against a known reference voltage. This is a 5-minute fix but it's safety-critical.

2. **Verify RAM budget** on the target STM32F4. The expanded data structures need a linker map review. Stack overflow in the monitor task would be silent and catastrophic.

3. **500 hours bench testing** with the mock HAL exercising all fault paths — sensor failures, I2C dropouts, pre-charge voltage mismatches, sub-zero charging, gas alarms. The test infrastructure (hal_mock.c) is already there. Use it.

4. **Field update mechanism** before production fleet deployment. JTAG-only updates are acceptable for prototype/sea trial vessels with a dedicated tech on board.

5. **NVM atomicity** fix before production. Acceptable risk for sea trials with monitoring.

---

## 6. What Impressed Me

Things I didn't ask for but got:

- **`_Static_assert` compile-time checks** for pack topology, IWDG timeout, sub-zero margin, and sensor count. Catches misconfiguration at compile time instead of at sea.
- **dT/dt thermal rate-of-rise detection** — this was Priya's finding, not mine, but it's the kind of defense-in-depth that catches problems I've seen in the field where absolute temperature thresholds alone weren't fast enough.
- **Gas/ventilation/fire integration** — complete safety I/O subsystem with CAN broadcast. This is what a real marine BMS needs.
- **IMD resistance trend logging** — predictive maintenance data I can actually use during annual surveys.
- **I2C bus recovery** on first comm failure. Simple, effective, saves unnecessary fault trips from transient EMI.
- **Fault reset rate limiting** (3 per hour) — prevents the "just keep resetting it" mentality I've seen from untrained crew.
- **Main loop structure in `main.c`** — clean cooperative scheduler with proper init sequence. IWDG started LAST, after all init completes. Contactors opened FIRST in init. The person who wrote this thinks about what happens during startup, not just steady-state.

---

## 7. What I Still Can't Assess

Same as V1 — I'm a field tech, not a firmware architect:

- RTOS correctness and priority inversion risks (NEW-04 is a concern)
- OCV curve accuracy for this specific cell chemistry
- Cryptographic adequacy of the CAN auth stub
- Classification society compliance (though it looks much closer now)
- Cell degradation compensation over time

---

## 8. Final Word

This rewrite took my "absolutely not" and turned it into a "yes, with conditions." That's a significant shift. The five criticals were addressed with understanding, not just compliance — the code comments reference specific scenarios from my review, which tells me the developer actually thought about *why* each fix matters.

The new safety I/O subsystem (gas, vent, fire, IMD) transforms this from "battery monitor" to "battery management system." That's the difference between code that works on a bench and code that works on a vessel.

Fix the bus voltage scaling inconsistency, verify the RAM budget, and this is ready for supervised sea trials. I'd want to be on the first few voyages with a laptop connected to the CAN bus, but I wouldn't be losing sleep over it.

Good work.

— Dave

---

*Delta review completed 2026-03-03. Based on source code comparison against V1 findings and 15 years of marine electrical installation experience. Not a certification assessment.*
