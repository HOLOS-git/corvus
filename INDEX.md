# INDEX — Corvus 🐦‍⬛

**Created:** 2026-02-27 | **Status:** Complete (v4 final) | **Last updated:** 2026-02-27

> "Mocking" — quickly dissecting real-world engineering problems and turning them into AI-built demos. Cheeky, competent, on-target.

---

## Overview

Corvus is a demonstration project: a working simulation of Corvus Energy's Orca ESS marine battery management system, grounded in their public integrator documentation. Built through 5 review cycles with ~40 AI critic agents across physics, code, manual compliance, safety, numerical methods, legal, and domain expert perspectives.

**Target:** Pack ⇄ EMS interface controller from the Orca ESS integrator documentation.
**Theme:** Corvid/mockingbird — we mock (in both senses) real engineering systems.
**Live site:** https://corvus-demo.netlify.app
**Public repo:** https://github.com/HOLOS-git/corvus

---

## File Inventory

### Python Implementation
| Path | Lines | Description |
|------|-------|-------------|
| `corvus_demo.py` | 1,356 | Complete v4 simulation — VirtualPack, PackController, ArrayController, 8-phase scenario |
| `corvus_plot.png` | — | Generated 5-panel plot (SoC, voltage, temperature, current/limits, modes) |
| `corvus_output.csv` | 1,347 | Time-series output from demo scenario (1,346 data rows + header) |

### C Port
| Path | Lines | Description |
|------|-------|-------------|
| `c/corvus_bms.h` | 278 | Public API — structs, enums, function prototypes |
| `c/corvus_bms.c` | 1,056 | Core BMS implementation — all physics and controls |
| `c/corvus_demo.c` | 436 | 8-phase scenario runner with CSV output |
| `c/test_corvus.c` | 713 | 20 test suites, 93 assertions |
| `c/Makefile` | 23 | Build targets: `make`, `make test`, `make debug` (ASan/UBSan) |

### Documentation
| Path | Lines | Description |
|------|-------|-------------|
| `README.md` | 160 | Project overview, parameters, how to run, 17 documented limitations |
| `LICENSE` | 21 | MIT License (2026 HOLOS-git) |
| `INDEX.md` | — | This file |

### Website
| Path | Lines | Description |
|------|-------|-------------|
| `site/index.html` | 592 | Crow narrator walkthrough — dark theme, speech bubbles, C section |
| `site/corvus_plot.png` | — | Plot copy for site embedding |

---

## What's Implemented

### Physics Model
- 24-point NMC 622 OCV curve (3.0–4.19V)
- 2D R(SoC, T) bilinear interpolation with U-shaped impedance profile (3.3 mΩ/module baseline from manual p.32)
- First-order thermal model: 1,268,000 J/°C composite mass, 800 W/°C forced-air cooling, 40°C ambient
- Kirchhoff bus model with iterative per-pack current clamping + KCL-correct equalization
- Coulombic efficiency (0.998 during charge)

### Controls
- 7-mode state machine (Table 15)
- Sequential first-pack pre-charge → simultaneous remaining (Section 7.2)
- Current limits: min(temp, SoC, SEV) from Figures 28/29/30
- Alarm system: SW faults (5s), HW safety independent (1s OV/UV, 5s OT), per Table 13
- Warning hysteresis with deadbands + 10s hold timer
- Fault latching with manual reset, 60s safe-state hold time
- Overcurrent warning (10s) + fault (charge-only at sub-zero)

### Scenario (8 phases)
1. Sequential pre-charge → parallel connection
2. Normal charging with Kirchhoff distribution
3. Equalization currents at zero load
4. Overcurrent warning demonstration
5. Cooling failure → thermal warning → fault
6. Warning hysteresis demonstration
7. Fault reset denied (hold time) → accepted
8. Reconnection → disconnect

---

## Build & Review History

| Version | Changes | Critics |
|---------|---------|---------|
| v1 | Initial build — 750 lines, basic state machine | 3 critics (physics, code, manual) |
| v2 | Tier 1 fixes + Tier 2 research integration | 3 critics |
| v3 | Simulation loop, HW safety, connect ordering, pre-charge | 6 critics (+integrator, safety, numerical) |
| v4 | Definitive build — all 15 fix categories merged | 6 critics |
| v4 polish | KCL fix, warnings, temp floor, limitations | 6 critics (+legal, fresh eyes) |
| v4 final | C port, electrochemistry fixes, site | 3 C-specific critics + Nick persona |

**Total:** ~40 sub-agent reviews, ~3,840 lines of code (Python + C), 93 test assertions, 17 documented limitations.

---

## Deployment

- **Netlify:** corvus-demo.netlify.app (site ID: ce32b1e9-16f8-4af4-bc42-aa657c4494ac)
- **GitHub:** HOLOS-git/corvus (public, MIT licensed)
- **Hawthorn:** projects/mockingbird/corvus/ on branch agent-state/hawthorn
