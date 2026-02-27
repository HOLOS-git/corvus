# INDEX — Corvus 🐦‍⬛

**Created:** 2026-02-27 | **Status:** Scoping

> "Mocking" — quickly dissecting real-world engineering problems and turning them into AI-built demos. Cheeky, competent, on-target.

---

## Overview

Corvus is a demonstration project: build a working software mock of a slice of **Corvus Energy's** marine battery management system, grounded entirely in their public documentation. The goal is to show a friend at Corvus how close an AI can get to their actual shipped interface contract — in an afternoon.

**Target:** Pack ⇄ EMS interface controller from the Orca ESS Integrator Manual.

**Theme:** Corvid/mockingbird — we mock (in both senses) real engineering systems.

## Reference Material

| Path | Description |
|------|-------------|
| `reference/pdfs/` | Original PDFs (Orca manual, Blue Whale datasheet, product brochure) |
| `reference/txt/` | Converted text versions for AI consumption |
| `corvus_plan.txt` | Perplexity research + initial scoping (32K tokens) |

### Key Source Documents
- **Orca ESS Integrator Manual** (1007768 Rev V, 113 pages) — the primary spec. Contains exact interface contract, pack modes, connect logic, current limits, alarm system, Modbus/CAN registers.
- **Blue Whale ESS Datasheet** — LFP chemistry, larger format systems
- **Product Brochure (Aug 2025)** — overview of Orca, Blue Whale, BOB product lines

## Demo Scope

Single-file Python demo implementing 3 documented Orca behaviors:

1. **Voltage-match-gated connect** — `|pack_voltage - bus_voltage| ≤ 1.2V × num_modules`
2. **Instantaneous current limits** — temperature, SoC, and SEV-based derating
3. **Persistent connect command handling** — command stays active until satisfied

Plus the full 7-mode pack state machine (OFF → POWER SAVE → FAULT → READY → CONNECTING → CONNECTED → NOT READY).

## Status

- [x] Reference material organized
- [x] Plan reviewed
- [ ] Implementation
- [ ] Test scenarios
- [ ] Documentation / writeup
