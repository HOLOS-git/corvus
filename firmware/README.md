# Corvus Orca ESS — BMS Firmware

Firmware architecture demo for a Corvus Orca Energy Storage System battery management system. Based on the BQ76952 battery monitor IC, targeting STM32F4 with a desktop mock HAL for testing.

> **Disclaimer:** Architecture demo only — not production code, not affiliated with or endorsed by Corvus Energy.

## Architecture

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

## Pack Topology

- 22 modules per pack, 14 series elements per module (308 cells)
- One BQ76952 ASIC per module, 3 thermistors per module
- Nominal capacity: 128 Ah

## Building

### Desktop (test)

```sh
make test      # build with mock HAL + run all tests
make debug     # build with ASan/UBSan
```

### STM32 (compile check)

```sh
make stm32     # requires arm-none-eabi-gcc
```

### Clean

```sh
make clean
```

## Key Design Choices

- **No floats** — all values are fixed-point (mV, mA, deci-°C, hundredths-%)
- **Leaky integrator timers** — fault timers increment under fault condition and decay at half-rate when clear, preventing transient nuisance trips
- **Per-cell protection** — individual OV/UV timers for all 308 cells
- **Contactor weld detection** — monitors current after open command
- **EMS watchdog** — 5s timeout forces safe state if EMS communication lost
- **C99, no heap** — all static allocation, pedantic warnings as errors
