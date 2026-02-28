# Phase 2: Firmware-Grade BMS

## Motivation

### Why We Went from Simulation to Firmware

Phase 1 delivered a Python simulation of the Corvus Orca ESS pack-EMS interface — a `VirtualPack`, `PackController`, and `EMSStub` demonstrating voltage-gated connect, instantaneous current limits, and persistent connect handling. The behaviors were grounded in the Orca ESS Integrator Manual and the code ran clean.

Nick (firmware engineer at Corvus) was unimpressed. His criticism boiled down to 14 points:

1. **No real MCU target** — Python isn't firmware; real BMS runs on ARM Cortex-M with bare-metal or RTOS
2. **Floating-point everywhere** — production BMS uses fixed-width integers (`uint16_t` millivolts, `int32_t` milliamps), not `float`/`double`
3. **No cell monitoring ASIC** — real packs use chips like the BQ76952; the sim had a lumped voltage model
4. **No per-cell monitoring** — real BMS tracks every cell individually (308 cells in an Orca pack), not one aggregate voltage
5. **No SPI/I2C driver code** — firmware engineers write register-level drivers; the sim had no device communication
6. **No RTOS task structure** — real BMS has priority-based tasks with deterministic timing, not a Python event loop
7. **No CAN bus framing** — the sim used Python structs; real systems pack CAN 2.0B frames byte-by-byte with scaling
8. **No contactor sequencing** — real systems have pre-charge, weld detection, feedback verification
9. **No HAL abstraction** — firmware separates hardware access behind a HAL so the same logic runs on target and desktop
10. **No fault timer logic** — real protection uses integrator timers with hysteresis, not instant threshold trips
11. **No watchdog** — EMS communication loss must trigger a safe state within bounded time
12. **No static allocation** — production firmware uses zero heap allocation (`malloc` forbidden)
13. **No cross-compilation** — real firmware builds for both ARM target and desktop test with the same source
14. **No datasheet traceability** — every register read and fault bit should cite a specific section of a real datasheet

### What "Firmware-Grade" Means vs "Simulation"

| Simulation (Phase 1) | Firmware (Phase 2) |
|---|---|
| Python, floating-point | C99, fixed-width integers only |
| Lumped battery model | Per-cell voltage/temp arrays (308 cells) |
| Generic `read_voltage()` | BQ76952 register-level I2C driver |
| No RTOS | FreeRTOS tasks with priority separation |
| Python dicts for messages | CAN 2.0B byte-packed frames |
| Instant threshold faults | Leaky integrator timers with hysteresis |
| Desktop only | Dual-target: `gcc` (desktop) + `arm-none-eabi-gcc` (STM32) |
| `malloc` used freely | Zero heap, all static allocation |
| No hardware abstraction | Mock HAL for testing, STM32 HAL for target |

---

## Research Sources

### 1. Corvus Orca ESS Integrator Manual
- **URL:** https://www.scribd.com/document/925876061/1007768-V-Orca-ESS-Integrator-Manual
- **What it provides:** The actual integration spec Corvus ships with Orca systems. Exact pack-EMS interface contract: Modbus TCP / CAN J1939 commands, connect/disconnect semantics, the "1.2V × modules" voltage-match rule, "instantaneous maximum allowable charge/discharge current" message, persistent connect command handling, 7 documented pack modes, fault/alarm behavior. Sections 3.2, 4.1, 5.3 most relevant.

### 2. TI BQ76952 Technical Reference Manual
- **URL (TRM):** https://www.ti.com/lit/ug/sluuby2b/sluuby2b.pdf
- **URL (Datasheet):** https://www.ti.com/lit/ds/symlink/bq76952.pdf
- **What it provides:** Complete register map (§12), cell voltage reading commands (§4.1, commands 0x14–0x32), OV/UV protection bits (§5.2.4–5.2.5), Safety Status A/B/C registers (0x03, 0x05, 0x06), fault status decoding (§12.2), battery status register (0x12), temperature measurement (internal 0x68, thermistors TS1/TS2/TS3), config update mode, subcommand protocol with checksums. The ASIC bible for cell monitoring.

### 3. STM32F446 Reference Manual
- **URL:** https://www.st.com/resource/en/reference_manual/dm00119316-stm32f446xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
- **What it provides:** SPI/I2C/CAN/ADC/GPIO peripheral register maps. Essential for HAL implementation. CAN peripheral (§35) for baud rate, filter, and mailbox configuration.

### 4. FreeRTOS Reference Manual
- **URL:** https://www.freertos.org/media/2018/FreeRTOS_Reference_Manual_V10.0.0.pdf
- **What it provides:** `xTaskCreate()` API (§4.2), priority-based preemptive scheduling, queue and timer APIs. Standard patterns for safety-critical periodic tasks.

### 5. BMS-CAN Protocol Spec (Akkudoktor)
- **URL:** https://akkudoktor.net/uploads/short-url/giWBklbWBknDzfd8KLV0KyqqfEc.pdf
- **What it provides:** Example CAN frame layouts for BMS status broadcast — message IDs, byte packing, scaling conventions. Used as inspiration for our CAN message design.

### 6. skriachko/bq76952 Reference Driver
- **URL:** https://github.com/skriachko/bq76952
- **What it provides:** Working STM32 + BQ76952 I2C driver (MIT license, verified on STM32WB55). I2C address conventions (0x10 write, 0x11 read), cell voltage command addresses (`CELL_NO_TO_ADDR(n) = 0x14 + (n * 2)`), subcommand read/write protocol with checksum, safety register bitfield definitions. Our register map and driver structure are directly informed by this.

### 7. EMSA BESS Guidance (Maritime Safety)
- **URL:** https://www.emsa.europa.eu/publications/inventories/download/7643/5061/23.html
- **What it provides:** Maritime BMS functional requirements — continuous BMS supervision (FR1–FR2), fault isolation behavior, emergency operation requirements. Shapes our fault-latching and safe-state semantics.

### 8. Blue Whale ESS Datasheet
- **URL:** https://old.corvusenergy.com/wp-content/uploads/2019/05/datasheets_Corvus-Energy_Blue-Whale-ESS_2025.02.23.pdf
- **What it provides:** Technical parameters (module count, voltage ranges, LFP chemistry details) confirming pack topology assumptions.

---

## Key Design Decisions

### Why STM32F4 + BQ76952

Both have fully public datasheets — every register, every peripheral, every timing constraint is documented. No NDA required. An STM32F446 is a real Cortex-M4F MCU with hardware FPU, CAN peripheral, and I2C/SPI — the same class of chip used in production BMS designs. The BQ76952 is TI's flagship 3–16 cell battery monitor with integrated protection, and its TRM is 300+ pages of register-level detail. Together they represent a realistic, defensible hardware platform where every line of driver code traces to a page number.

### Fixed-Width Types Only

All values use explicit-width types with physical units in the name:
- `uint16_t cell_mv[NUM_CELLS]` — millivolts per cell
- `int32_t pack_current_ma` — milliamps (signed: positive = discharge)
- `int16_t temp_deci_c` — temperature in 0.1°C units
- `uint16_t soc_pct_100` — state of charge in hundredths of percent (0–10000)

No `float`, no `double`, no ambiguous units. This is how production firmware works — it eliminates floating-point non-determinism and makes every value's physical meaning explicit.

### Per-Cell Monitoring vs Lumped Model

An Orca pack has 22 modules × 14 series elements = 308 cells. The firmware tracks voltage and temperature for every cell individually, not as a single lumped value. This is essential because:
- Cell imbalance is the primary degradation/safety concern
- OV/UV protection must act on the worst cell, not the average
- The BQ76952 natively provides per-cell readings — using a lumped model wastes the ASIC's capabilities

Each of the 22 modules has its own BQ76952 (the chip handles up to 16 cells; 14 cells per module fits within that).

### Mock HAL for Testability

All hardware access goes through a HAL layer (`hal.h`). Two implementations exist:
- `hal_mock.c` — desktop build, allows tests to inject cell voltages, temperatures, and fault conditions
- `hal_stm32f4.c` — target build, wraps STM32 HAL/LL drivers

This means the same `bms_monitor.c`, `bms_protection.c`, etc. compile and run identically on a laptop or an STM32. Tests exercise real fault logic without hardware.

### FreeRTOS Task Structure

Three tasks with priority separation:
- **Safety Monitor** (highest priority, 10ms period) — reads cells, evaluates faults, controls contactors
- **CAN Comms** (medium priority, 100ms period) — packs and sends status frames
- **Housekeeping** (lowest priority) — diagnostics, watchdog kicks

This mirrors the standard RTOS pattern for safety-critical embedded: the safety task always preempts communication, and communication always preempts housekeeping.

---

## What Was Built

### Architecture Overview

```
src/
  bms_bq76952.c    — BQ76952 I2C driver (cell voltage, temp, safety registers)
  bms_monitor.c    — Pack-level data aggregation from all 22 modules
  bms_protection.c — Per-cell fault detection with leaky integrator timers
  bms_contactor.c  — Contactor state machine (pre-charge, weld detection)
  bms_can.c        — CAN 2.0B message framing (Orca Modbus register map)
  bms_state.c      — 7-mode pack state machine (OFF→NOT_READY→READY→…)

inc/               — Headers and type definitions
hal/
  hal_mock.c       — Desktop mock HAL with injectable state for testing
  hal_stm32f4.c    — STM32F4 HAL (compile-check only without full SDK)
rtos/
  bms_tasks.c      — FreeRTOS task wrappers
test/
  test_main.c      — Minimal test runner (no external framework)
  test_bq76952.c   — BQ76952 driver tests
  test_protection.c— Fault detection + leaky timer tests
  test_contactor.c — Contactor state machine + weld detection tests
  test_can.c       — CAN frame encode/decode tests
  test_state.c     — Pack state machine transition tests
```

### Module Descriptions

| Module | Description |
|---|---|
| `bms_bq76952` | I2C driver for TI BQ76952. Reads cell voltages (commands 0x14–0x32), temperatures (0x68, 0x70–0x76), safety status registers (0x03, 0x05, 0x06), battery status (0x12). Implements subcommand protocol with checksum per TRM §12. |
| `bms_monitor` | Aggregates readings from all 22 BQ76952 ASICs. Computes pack-level min/max/avg cell voltage, max temperature, pack current. Maintains the `bms_pack_data_t` struct that all other modules consume. |
| `bms_protection` | Per-cell OV/UV fault detection using leaky integrator timers. Timers increment under fault condition and decay at half-rate when clear, preventing transient nuisance trips. Individual timers for all 308 cells. |
| `bms_contactor` | State machine for contactor control: OPEN → PRE_CHARGE → CLOSING → CLOSED → OPENING → WELDED. Pre-charge sequencing with timeout. Weld detection by monitoring current after open command. |
| `bms_can` | CAN 2.0B message encoding/decoding. Packs status frames with millivolt/milliamp scaling. Includes EMS watchdog — 5s timeout forces safe state if no EMS heartbeat received. |
| `bms_state` | 7-mode pack state machine matching Orca ESS documented modes: OFF, NOT_READY, READY, STANDBY, CONNECTED, FAULT, SHUTDOWN. Transition guards enforce safety preconditions. |
| `hal_mock` | Desktop test HAL. All `hal_*` functions route to injectable state arrays. Tests set cell voltages, inject faults, verify GPIO outputs, capture CAN frames — all without hardware. |

### Test Coverage

All tests run on desktop via `make test` using the mock HAL:

- **BQ76952 driver tests** — verify I2C command sequences, voltage scaling, temperature conversion (raw 0.1K → deci-°C), safety register parsing
- **Protection tests** — verify leaky integrator behavior: fault detection after sustained OV/UV, timer decay on clear, no nuisance trip on transient glitch
- **Contactor tests** — verify pre-charge sequencing, weld detection (current persists after open), timeout handling
- **CAN tests** — verify frame encoding (byte order, scaling), decoding round-trip, EMS watchdog timeout
- **State machine tests** — verify all legal transitions, verify illegal transitions are rejected, verify fault latching requires explicit reset

Build also supports `make debug` (ASan/UBSan) and `make stm32` (ARM cross-compile check).

---

## Nick's Original Criticism → How Each Point Was Addressed

| # | Nick's Criticism | Phase 2 Response |
|---|---|---|
| 1 | No real MCU target | Builds for STM32F4 via `arm-none-eabi-gcc`. Makefile has `stm32` target. |
| 2 | Floating-point everywhere | Zero floats. All values: `uint16_t` mV, `int32_t` mA, `int16_t` deci-°C, `uint16_t` hundredths-%. |
| 3 | No cell monitoring ASIC | Full BQ76952 I2C driver from TRM register map (commands 0x14–0x32, safety regs 0x03/0x05/0x06). |
| 4 | No per-cell monitoring | 308 cells tracked individually. Per-cell voltage and temperature arrays. Per-cell fault timers. |
| 5 | No SPI/I2C driver code | Register-level I2C driver with subcommand protocol, checksums, config update mode per BQ76952 TRM §12. |
| 6 | No RTOS task structure | FreeRTOS tasks: safety monitor (10ms, highest priority), CAN comms (100ms), housekeeping (lowest). |
| 7 | No CAN bus framing | CAN 2.0B byte-packed frames with millivolt/milliamp scaling, J1939-style message IDs. |
| 8 | No contactor sequencing | Full state machine: OPEN → PRE_CHARGE → CLOSING → CLOSED → OPENING → WELDED. Weld detection. |
| 9 | No HAL abstraction | `hal.h` with two implementations: `hal_mock.c` (desktop) and `hal_stm32f4.c` (target). |
| 10 | No fault timer logic | Leaky integrator timers: increment under fault, decay at half-rate on clear. Per-cell, prevents nuisance trips. |
| 11 | No watchdog | EMS watchdog: 5s timeout forces safe state on communication loss. |
| 12 | No static allocation | C99, zero heap. All static allocation. `-Werror -pedantic`. |
| 13 | No cross-compilation | Makefile builds both `gcc` (desktop test) and `arm-none-eabi-gcc` (STM32 target). |
| 14 | No datasheet traceability | Every register address, fault bit, and command cites BQ76952 TRM section numbers. Orca manual cited for pack behavior. |

---

## What's Still Missing (Honest)

### Things a Real Product Would Have

- **Coulomb counting + SoC estimation** — real SoC algorithms use extended Kalman filters or adaptive observers, not simple coulomb counting. We have the cell data infrastructure but not the estimation algorithm.
- **Cell balancing** — the BQ76952 supports passive balancing; we read cells but don't implement balance control logic.
- **Thermal management** — liquid cooling loop control (pump/valve sequencing) is a significant subsystem we don't model.
- **Modbus TCP protocol stack** — the Orca manual specifies Modbus TCP as the primary EMS interface. We implemented CAN framing but not the full Modbus register map.
- **Multi-pack coordination** — real Orca arrays have multiple packs sharing a DC bus. We model a single pack.
- **Power-on self-test (POST)** — production firmware runs comprehensive diagnostics at startup.
- **OTA update mechanism** — bootloader and firmware update path.
- **EMI/EMC hardening** — software filters for noisy marine electrical environments.
- **IEC 62619 / IEC 62620 compliance** — formal safety analysis (FMEA, diagnostic coverage calculations).
- **Aging/degradation models** — SoH tracking over pack lifetime.
- **Ground fault detection** — insulation resistance monitoring (mentioned in Orca manual but not implemented).
- **Black-start logic** — the Orca manual documents a black-start capability for packs; we don't implement it.

### Things We'd Need Proprietary Info For

- **Exact CAN DBC file** — Corvus' actual CAN message IDs, signal definitions, and byte layouts
- **Exact Modbus register map** — the integrator manual describes behavior but doesn't publish the full register table
- **Which cell monitoring ASIC Corvus actually uses** — we chose BQ76952 as representative; they may use ADI ADBMS6830, custom silicon, or something else entirely
- **Derating curves** — the exact SoC/temperature → current limit curves are tuned to their cells and qualification testing
- **Cell characterization data** — OCV-SoC curves, internal resistance vs temperature, specific to their cell supplier
- **Safety case documentation** — formal hazard analysis, failure mode tables, diagnostic coverage ratios
- **Fleet analytics algorithms** — their cloud-based SoH prediction and operational optimization

---

## Next Steps (If We Continue)

1. **Coulomb counting + basic SoC** — implement simple coulomb counting with the per-cell voltage data we already collect; add OCV-based SoC correction at rest
2. **Passive cell balancing** — the BQ76952 supports it natively; add balance control logic using the existing driver
3. **Modbus TCP register map** — implement the EMS-facing Modbus interface to match the Orca integrator manual's documented commands (CONNECT, DISCONNECT, CONNECT_ALL_TO_CHARGE, etc.)
4. **Multi-pack array simulation** — extend the mock HAL to simulate 4+ packs on a shared DC bus with the voltage-match connect logic
5. **QEMU STM32 emulation** — run the actual ARM binary in QEMU with mock peripherals for true hardware-in-the-loop testing without physical boards
6. **Ground fault monitor** — add insulation resistance measurement using the BQ76952's auxiliary ADC channels
7. **Flash a real board** — STM32 Nucleo-F446RE boards cost ~$15; flash the firmware and demonstrate it running on actual hardware with simulated BQ76952 responses over I2C
8. **Formal safety analysis** — create a basic FMEA table mapping fault modes to detection mechanisms and safe states, aligned with IEC 62619 structure
