# Corvus Orca ESS — Street Smart Edition

> A ground-up rewrite of the Corvus BMS firmware incorporating findings from a 6-reviewer adversarial panel (Mockingbird framework).

## What Changed

The original firmware was competent embedded code but had critical gaps identified by:
- 🔧 **Field Tech** — 5 critical field-deployment issues
- 📋 **Insurance Underwriter** — €50M max credible loss scenarios
- ⚔️ **Competitor Engineer** — architecturally a generation behind
- 🔍 **Class Surveyor** — 6 FAIL findings against DNV rules
- 💀 **Pentester** — CRITICAL risk rating, 13 vulnerabilities
- 🔥 **Thermal Engineer** — thermal model inadequate, cooling 2-4x optimistic

## Key Improvements
- True HW safety independence (BQ76952 autonomous protection configured)
- CAN message authentication (CMAC)
- Gas detection / ventilation integration
- dT/dt thermal runaway detection
- Sensor fault detection (dead sensor != "normal")
- Secure bootloader with firmware signing
- Insulation monitoring interface
- Comprehensive fault logging with timestamps

## Reviews
See `../masks/*/FULL_REVIEW.md` for the full adversarial review reports.
See `../corvus/SYNTHESIS.md` and `IMPLEMENTATION_PLAN.md` for the unified findings.
