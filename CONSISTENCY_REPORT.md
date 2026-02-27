# Corvus Orca ESS — M-11 Phase 2 Self-Consistency Report

**Date:** 2026-02-27  
**Scope:** All project files cross-checked for constant, claim, cross-reference, terminology, and Python↔C parity consistency.

---

## Summary

| Severity | Count |
|----------|-------|
| CRITICAL | 3 |
| MEDIUM   | 5 |
| LOW      | 4 |

---

## CRITICAL

### C1. README thermal mass contradicts code and RESEARCH.md

**Files:** `README.md` (lines ~41, ~133, ~139) vs `corvus_demo.py` (line 78), `c/corvus_bms.h` (line 54), `RESEARCH.md` (section 2)

- **README v4 change #2:** "Realistic thermal mass — **1,386,000 J/°C** (22 modules × 60 kg × 1050 J/kg/K)"
- **README Key Parameters table:** "Thermal mass | **1,386,000 J/°C** | 22 × 60 kg × 1050 J/(kg·K)"
- **README Architecture table:** "first-order thermal (**1.39 MJ/°C**)"
- **Code (Python & C):** `THERMAL_MASS = 1_268_000` / `BMS_THERMAL_MASS = 1268000.0`
- **RESEARCH.md:** "THERMAL_MASS ≈ 924 × 1050 + 596 × 500 ≈ **1,268,200 J/°C ≈ 1.27 MJ/°C**"

README uses the naïve single-material calculation (22 × 60 × 1050 = 1,386,000). Code and RESEARCH.md correctly use the composite calculation (70% cells at 1050 + 30% non-cell at 500 = 1,268,000). The README is stale from before the composite model was adopted.

**Fix:** Update README to say "1,268,000 J/°C" with the composite breakdown, and architecture table to "1.27 MJ/°C".

---

### C2. Site OCV curve last value 4.175 vs code's 4.190

**Files:** `site/index.html` (OCV code block) vs `corvus_demo.py` (line 120), `c/corvus_bms.c` (line 145), `RESEARCH.md` (section 3)

The site's Python code snippet shows:
```
_OCV_BP = [..., 4.100, 4.175]
```

All other sources (Python, C, RESEARCH.md) have:
```
_OCV_BP = [..., 4.100, 4.190]
```

The site displays an outdated OCV curve. 4.190V at 100% SoC is the correct NMC 622 value and matches `SE_OVER_VOLTAGE_WARNING = 4.210V` (20 mV above OCV at 100%).

**Fix:** Change `4.175` to `4.190` in `site/index.html`.

---

### C3. Site narrative claims R = 13.2 mΩ at -10°C/5% SoC; actual is 15.3 mΩ

**Files:** `site/index.html` (Section 2 bubble text) vs resistance table in same file, `corvus_demo.py` (line 98), `c/corvus_bms.c` (line 84), `RESEARCH.md` (section 1)

Site narrative: *"At -10°C and 5% SoC? **13.2 mΩ**. That's 4× higher."*

The resistance table on the same page, and all code/research files, show **15.3 mΩ** at SoC 5%/-10°C. 15.3 / 3.3 ≈ 4.6×, not 4×. The 13.2 figure doesn't appear anywhere in the codebase.

**Fix:** Change "13.2 mΩ" to "15.3 mΩ" and "4×" to "nearly 5×" in the site narrative.

---

## MEDIUM

### M1. README limitation #5 says 264 cells; code uses 308

**Files:** `README.md` (limitation #5, line ~148) vs `corvus_demo.py` (line 70), `c/corvus_bms.h` (line 45)

README: *"Real Orca ESS monitors each of **264** cells individually (Section 7.6.1)."*

Code: `NUM_CELLS_SERIES = 22 × 14 = 308`. The 264 figure comes from an earlier assumption of 12 cells/module (22 × 12 = 264).

**Fix:** Change "264" to "308" in README limitation #5. (Or if 264 is the manual's actual number for a different config, clarify which configuration.)

---

### M2. FIX_PLAN says 12 cells/module; code and RESEARCH say 14

**Files:** `FIX_PLAN.md` (section 3.1, line ~134) vs `corvus_demo.py` (line 67), `RESEARCH.md` (section 1)

FIX_PLAN: *"Each module has **12 cells**, each monitored individually."*

Code and RESEARCH.md: `CELLS_PER_MODULE = 14` (derived from 50V / 3.6V nominal).

FIX_PLAN was written before the RESEARCH.md analysis established 14 cells/module.

**Fix:** Update FIX_PLAN section 3.1 to say "14 cells" and "308 cells" (or note it's from the pre-research version).

---

### M3. RESEARCH.md recommends AMBIENT_TEMP = 25°C; code uses 40°C

**Files:** `RESEARCH.md` (section 2, "Recommended Values for Demo") vs `corvus_demo.py` (line 81), `c/corvus_bms.h` (line 56)

RESEARCH.md section 2 recommends: `AMBIENT_TEMP = 25.0  # °C`

Code uses: `AMBIENT_TEMP = 40.0  # °C -- realistic engine room`

The code intentionally changed to 40°C for realism (documented in Python docstring line 30 and README). RESEARCH.md's recommendation section was not updated.

**Fix:** Update RESEARCH.md section 2 recommended values to show `AMBIENT_TEMP = 40.0` with note about engine room realism, or add a note that the demo overrides this.

---

### M4. INDEX.md says "6-panel plot"; code generates 5 panels

**Files:** `INDEX.md` (Python Implementation table) vs `corvus_demo.py` (line 1289: `plt.subplots(5, 1, ...)`)

INDEX: "Generated **6-panel** plot (SoC, voltage, temperature, current limits, modes, per-pack current)"

Code creates 5 subplots: SoC, cell voltage, temperature, current, modes. There is no separate "per-pack current" panel vs "current limits" panel — they're combined.

**Fix:** Change "6-panel" to "5-panel" and update the parenthetical list to "(SoC, voltage, temperature, current/limits, modes)".

---

### M5. README Architecture table thermal mass also wrong

**Files:** `README.md` Architecture table

The Architecture table says: "OCV(SoC) + R(SoC,T) equivalent circuit, coulomb counting, first-order thermal (**1.39 MJ/°C**)"

1.39 MJ/°C = 1,390,000 ≈ 1,386,000 (the naïve calc). Should be 1.27 MJ/°C per the composite model.

**Fix:** Change "1.39 MJ/°C" to "1.27 MJ/°C" (same root cause as C1).

---

## LOW

### L1. README CSV row count "~865" is outdated

**Files:** `README.md` (How to Run section) vs `INDEX.md`

README: "corvus_output.csv — time series of all pack states (**~865 rows**)"
INDEX: "corvus_output.csv | **1,346** | Time-series output from demo scenario"

The scenario was extended in v4; the actual row count grew. Neither may be exactly right for the current code.

**Fix:** Update README to "~1,350 rows" or remove the specific count.

---

### L2. Site says "30 critics"; INDEX says "~40 sub-agent reviews"

**Files:** `site/index.html` (hero, section 7, footer) vs `INDEX.md` (Build & Review History)

Site: "30 critics" / "30 reviewers"
INDEX: "~40 sub-agent reviews"

Minor marketing inconsistency.

**Fix:** Align on one number. INDEX's "~40" likely includes all review rounds; "30" may be unique critic personas.

---

### L3. README Key Parameters: "R_module (25°C, mid-SoC) | 3.3 mΩ" — table mid-SoC is 3.1

**Files:** `README.md` Key Parameters table vs resistance lookup table in code

README says the 3.3 mΩ baseline is at "25°C, mid-SoC." The R table at 25°C/50% SoC gives **3.1 mΩ**. The 3.3 value appears at 25°C/20% and 25°C/35%.

The 3.3 mΩ is the **manufacturer's stated value** (manual p.32), not a table lookup result. The description is slightly misleading.

**Fix:** Change to "R_module (nominal) | 3.3 mΩ | Corvus manual p.32" or "R_module (25°C, 20–35% SoC) | 3.3 mΩ".

---

### L4. RESEARCH.md Summary table: pack voltage range "~924-1286 V" vs OCV table "924-1291 V"

**Files:** `RESEARCH.md` — Summary table says "~924-1286 V", but section 3 OCV table shows 100% = 4.190V × 308 = 1290.5 ≈ 1291V.

Minor rounding inconsistency (1286 vs 1291). The 1286 may be from an earlier OCV endpoint.

**Fix:** Update Summary table to "~924–1291 V".

---

## Consistency Checks — PASSED ✓

The following areas were checked and found consistent:

| Check | Status |
|-------|--------|
| **All alarm thresholds (Table 13)** — Python, C, site HTML, README | ✓ Match across all files |
| **OCV breakpoints** — Python ↔ C ↔ RESEARCH.md (24 points) | ✓ Identical (site has one wrong value — see C2) |
| **Resistance table** — Python ↔ C ↔ RESEARCH.md ↔ site (7×6 values) | ✓ All 42 values match |
| **Figure 28 temp breakpoints** — Python ↔ C ↔ RESEARCH.md | ✓ Charge (8 points) and discharge (15 points) match |
| **Figure 29 SoC breakpoints** — Python ↔ C ↔ RESEARCH.md | ✓ Charge (5 points) and discharge (9 points) match |
| **Figure 30 SEV breakpoints** — Python ↔ C ↔ RESEARCH.md | ✓ Charge (3 points) and discharge (7 points) match |
| **HW safety thresholds** — Python ↔ C ↔ site | ✓ OV=4.300V/1s, UV=2.700V/1s, OT=70.0°C/5s |
| **Warning hysteresis deadbands** — Python ↔ C | ✓ OV clear=4.190V, UV clear=3.220V, OT clear=57.0°C |
| **Coulombic efficiency** — Python (0.998) ↔ C (hardcoded 0.998) | ✓ |
| **Pre-charge duration** — Python ↔ C ↔ README ↔ site | ✓ 5.0s |
| **Warning hold time** — Python ↔ C ↔ README | ✓ 10.0s |
| **Fault reset hold time** — Python ↔ C ↔ README ↔ site | ✓ 60.0s |
| **Voltage match** — Python ↔ C ↔ site | ✓ 1.2V/module |
| **Pack parameters** — 22 modules, 14 cells/module, 128 Ah | ✓ Python ↔ C ↔ RESEARCH.md |
| **Cooling coefficient** — Python ↔ C ↔ RESEARCH.md ↔ site | ✓ 800 W/°C |
| **Ambient temperature** — Python ↔ C ↔ README ↔ site | ✓ 40°C |
| **Pack mode enum names** — Python ↔ C ↔ site | ✓ Same 7 modes, same section citations |
| **INDEX.md line counts** — all 9 files | ✓ All match `wc -l` output exactly |
| **INDEX.md file paths** — all listed files exist | ✓ |
| **Python ↔ C algorithm parity** — Kirchhoff solver, equalization, alarm logic, state machine | ✓ Same algorithm structure, same iteration caps, same clamp logic |
| **Section citations in code** — Table 13, 15, 16; Sections 6.2, 6.3.5, 7.1, 7.2, 7.4 | ✓ Valid references |
| **Terminology** — "warning"/"fault"/"HW safety" used consistently | ✓ |
| **Sign convention** — positive = charging, consistent everywhere | ✓ |
| **C Makefile targets** — mentioned in INDEX, file exists | ✓ |
| **LICENSE** — mentioned in INDEX, file exists | ✓ |

---

## Recommended Fix Priority

1. **C1 + M5** — README thermal mass (one edit, two locations) — CRITICAL
2. **C2** — Site OCV endpoint 4.175 → 4.190 — CRITICAL
3. **C3** — Site resistance narrative 13.2 → 15.3 mΩ — CRITICAL
4. **M1** — README 264 → 308 cells — MEDIUM
5. **M3** — RESEARCH.md ambient temp recommendation — MEDIUM
6. **M4** — INDEX "6-panel" → "5-panel" — MEDIUM
7. **M2** — FIX_PLAN 12 → 14 cells/module — MEDIUM (low urgency, FIX_PLAN is historical)
8. **L1–L4** — Minor text updates — LOW
