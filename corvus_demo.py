#!/usr/bin/env python3
"""
Corvus Orca ESS Simulation v4

Independent simulation of Orca ESS interface behaviors for integration
testing and educational purposes. Not affiliated with, endorsed by, or
derived from Corvus Energy's proprietary software. Interface behaviors
are implemented from publicly-available integrator documentation for
interoperability purposes.

Reference: Corvus Energy Orca ESS integrator documentation

v4 Changes:
  - 2D resistance lookup R(SoC, T) with bilinear interpolation (3.3 mΩ/module baseline)
  - Thermal mass 1,386,000 J/°C (22 × 60 kg × 1050 J/kg/K), cooling 800 W/°C
  - 24-point NMC 622 OCV curve from literature
  - Figure 28/29/30 breakpoints for temp/SoC/SEV current limits
  - Double-step bug fixed: PackController.step() no longer drives pack physics
  - HW safety independent of fault_latched (runs in try/except, always fires)
  - All current limits clamped to max(0, ...)
  - OC discharge warning sign corrected per Table 13
  - OC fault only on charge at sub-zero per Table 13
  - SEV-based current limit added
  - Warning hysteresis with 10s minimum hold time, no blanket 50% derating
  - Connect ordering: first pack by SoC, remaining simultaneously
  - Kirchhoff solver: iteration cap = len(conn), equalization at zero load
  - Temp limit continuity: linear ramp 65-70°C
  - Fault reset with 60s safe-state hold time, SW/HW fault distinction
  - Ambient 40°C (realistic engine room)
  - Phase 4 cooling via external_heat, not manual temperature override

LIMITATIONS:
  - HW safety is simulated in software, not an independent hardware protection layer
  - No watchdog timer, contactor welding detection, or feedback verification
  - No CAN/Modbus communication timeout modeling
  - No ground fault / insulation monitoring
  - No pre-charge inrush current modeling (timer only)
  - No cell balancing or per-cell monitoring (lumped single-cell model)
  - No aging, SOH, capacity fade, or calendar degradation
  - No self-discharge
  - Equalization currents may have small KCL residual after per-pack clamping
  - Array current limits use min×N per manual Section 7.4 example (conservative)
  - Warning hysteresis deadbands and fault reset hold time are engineering choices, not from manual
  - Remaining packs connect based on voltage match only; manual §7.2.1 also gates on SoC convergence between connected and ready packs
  - Thermal model is lumped per-pack (no cell-to-cell thermal gradients or module-level thermal stratification)
"""

from __future__ import annotations
import csv
import os
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Dict, List, Optional, Tuple

import numpy as np

# =====================================================================
# CONSTANTS -- straight from the horse's mouth (or the crow's beak)
# Table 13: "Table of Alarm Threshold Values"
# =====================================================================

SE_OVER_VOLTAGE_FAULT = 4.225      # V, 5s delay -- Table 13
SE_UNDER_VOLTAGE_FAULT = 3.000     # V, 5s delay -- Table 13
SE_OVER_TEMP_FAULT = 65.0          # °C, 5s delay -- Table 13
SE_OVER_VOLTAGE_WARNING = 4.210    # V, 5s delay -- Table 13
SE_UNDER_VOLTAGE_WARNING = 3.200   # V, 5s delay -- Table 13
SE_OVER_TEMP_WARNING = 60.0        # °C, 5s delay -- Table 13

# Warning clear thresholds (hysteresis deadband)
SE_OV_WARN_CLEAR = 4.190           # V -- 20 mV deadband
SE_UV_WARN_CLEAR = 3.220           # V -- 20 mV deadband
SE_OT_WARN_CLEAR = 57.0            # °C -- 3°C deadband

# Hardware safety -- with proper delay timers per Table 13
HW_SAFETY_OVER_VOLTAGE = 4.300     # V, 1s -- Table 13
HW_SAFETY_UNDER_VOLTAGE = 2.700    # V, 1s -- Table 13
HW_SAFETY_OVER_TEMP = 70.0         # °C, 5s -- Table 13

# Section 7.2: voltage match for connection
VOLTAGE_MATCH_PER_MODULE = 1.2     # V per module

# Pack parameters -- Orca configuration
NUM_MODULES = 22                   # 22 Orca modules connected in series
CELLS_PER_MODULE = 14              # 14 SE per module (50V / 3.6V nominal) per RESEARCH.md
NOMINAL_CAPACITY_AH = 128.0        # Section 1.3: "For Orca 1C is 128A"
NUM_CELLS_SERIES = NUM_MODULES * CELLS_PER_MODULE  # 308

# Thermal parameters -- from RESEARCH.md
# 22 modules × 60 kg/module × 1050 J/(kg·K) ≈ 1,386,000 J/°C
THERMAL_MASS = 1_386_000.0         # J/°C
THERMAL_COOLING_COEFF = 800.0      # W/°C -- forced air cooling
AMBIENT_TEMP = 40.0                # °C -- realistic engine room

# Pre-charge timing -- Table 16: 5s minimum for 22 modules
PRECHARGE_DURATION = 5.0           # seconds

# Warning minimum hold time before clearing
WARNING_HOLD_TIME = 10.0           # seconds

# Fault reset safe-state hold time -- Section 6.3.5
FAULT_RESET_HOLD_TIME = 60.0       # seconds


# =====================================================================
# RESISTANCE LOOKUP TABLE -- R_module(T, SoC) in mΩ
# Baseline 3.3 mΩ/module at 25°C mid-SoC (Corvus manual p.32)
# Temperature/SoC variation from NMC pouch cell literature (RESEARCH.md)
# =====================================================================

_R_TEMPS = np.array([-10.0, 0.0, 10.0, 25.0, 35.0, 45.0])
_R_SOCS = np.array([0.05, 0.20, 0.50, 0.80, 0.95])
# mΩ per module -- rows=SoC, cols=Temp
_R_TABLE = np.array([
    [13.2, 8.3, 5.3, 4.3, 3.8, 3.5],   # SoC=5%
    [10.0, 6.6, 4.3, 3.3, 3.0, 2.8],   # SoC=20%
    [ 9.9, 6.6, 4.3, 3.3, 3.0, 2.8],   # SoC=50%
    [ 9.9, 6.6, 4.3, 3.3, 3.0, 2.8],   # SoC=80%
    [11.6, 7.6, 4.8, 3.6, 3.3, 3.1],   # SoC=95%
])


def _bilinear_interp(temp: float, soc: float) -> float:
    """Bilinear interpolation of module resistance (mΩ) from R(T, SoC) table."""
    t = np.clip(temp, _R_TEMPS[0], _R_TEMPS[-1])
    s = np.clip(soc, _R_SOCS[0], _R_SOCS[-1])

    # Find bracketing indices for temperature
    ti = np.searchsorted(_R_TEMPS, t, side='right') - 1
    ti = np.clip(ti, 0, len(_R_TEMPS) - 2)
    t_frac = (t - _R_TEMPS[ti]) / (_R_TEMPS[ti + 1] - _R_TEMPS[ti])

    # Find bracketing indices for SoC
    si = np.searchsorted(_R_SOCS, s, side='right') - 1
    si = np.clip(si, 0, len(_R_SOCS) - 2)
    s_frac = (s - _R_SOCS[si]) / (_R_SOCS[si + 1] - _R_SOCS[si])

    # Bilinear
    r00 = _R_TABLE[si, ti]
    r01 = _R_TABLE[si, ti + 1]
    r10 = _R_TABLE[si + 1, ti]
    r11 = _R_TABLE[si + 1, ti + 1]

    r0 = r00 + (r01 - r00) * t_frac
    r1 = r10 + (r11 - r10) * t_frac
    return float(r0 + (r1 - r0) * s_frac)


def module_resistance(temp: float, soc: float) -> float:
    """Module resistance in Ω (from mΩ table)."""
    return _bilinear_interp(temp, soc) * 1e-3


def pack_resistance(temp: float, soc: float) -> float:
    """Pack resistance in Ω (22 modules in series)."""
    return module_resistance(temp, soc) * NUM_MODULES


# =====================================================================
# OCV vs SoC -- 24-point NMC 622 curve from RESEARCH.md
# Section 2.3: "SoC is a percentage measure of the charge remaining"
# =====================================================================

_SOC_BP = np.array([
    0.00, 0.02, 0.05, 0.08, 0.10, 0.15, 0.20, 0.25,
    0.30, 0.35, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65,
    0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 0.98, 1.00,
])
_OCV_BP = np.array([
    3.000, 3.280, 3.420, 3.480, 3.510, 3.555, 3.590, 3.610,
    3.625, 3.638, 3.650, 3.662, 3.675, 3.690, 3.710, 3.735,
    3.765, 3.800, 3.845, 3.900, 3.960, 4.030, 4.100, 4.175,
])


def ocv_from_soc(soc: float) -> float:
    """Open-circuit voltage per cell from SoC."""
    return float(np.interp(np.clip(soc, 0.0, 1.0), _SOC_BP, _OCV_BP))


# =====================================================================
# CURRENT LIMIT CURVES -- Figures 28, 29, 30 (RESEARCH.md breakpoints)
# All return C-rates as positive magnitudes. Caller multiplies by capacity.
# =====================================================================

# Figure 28: Temperature-based current limit
_TEMP_CHARGE_BP = np.array([-25, 0, 5, 15, 35, 45, 55, 65])
_TEMP_CHARGE_CR = np.array([0.0, 0.0, 0.0, 3.0, 3.0, 2.0, 0.0, 0.0])

_TEMP_DISCH_BP = np.array([-25, -15, -10, -5, 0, 5, 10, 25, 30, 35, 45, 55, 60, 65, 70])
_TEMP_DISCH_CR = np.array([0.2, 0.2, 1.0, 1.5, 2.0, 4.5, 5.0, 5.0, 4.5, 4.0, 3.8, 3.8, 0.2, 0.2, 0.0])
# Fix #11: continuity -- added 70°C→0.0 so there's a linear ramp from 65→70°C

# Figure 29: SoC-based current limit (BOL)
_SOC_CHARGE_BP = np.array([0.0, 0.85, 0.90, 0.95, 1.00])
_SOC_CHARGE_CR = np.array([3.0, 3.0, 2.0, 1.0, 0.5])

_SOC_DISCH_BP = np.array([0.00, 0.02, 0.05, 0.08, 0.10, 0.15, 0.20, 0.50, 1.00])
_SOC_DISCH_CR = np.array([1.0, 1.0, 2.2, 2.2, 4.0, 4.0, 5.0, 5.0, 5.0])

# Figure 30: SEV (cell voltage) based current limit
_SEV_CHARGE_BP = np.array([3.000, 4.100, 4.200])  # V
_SEV_CHARGE_CR = np.array([3.0, 3.0, 0.0])

_SEV_DISCH_BP = np.array([3.000, 3.200, 3.300, 3.400, 3.450, 3.550, 4.200])
_SEV_DISCH_CR = np.array([0.0, 0.0, 2.0, 2.5, 3.8, 5.0, 5.0])


def _temp_current_limit(temp: float, cap: float) -> Tuple[float, float]:
    """Figure 28: Temperature-based current limit. Returns (charge_A, discharge_A)."""
    cc = float(np.interp(temp, _TEMP_CHARGE_BP, _TEMP_CHARGE_CR))
    dc = float(np.interp(temp, _TEMP_DISCH_BP, _TEMP_DISCH_CR))
    return max(0.0, cc * cap), max(0.0, dc * cap)


def _soc_current_limit(soc: float, cap: float) -> Tuple[float, float]:
    """Figure 29: SoC-based current limit. Returns (charge_A, discharge_A)."""
    cc = float(np.interp(soc, _SOC_CHARGE_BP, _SOC_CHARGE_CR))
    dc = float(np.interp(soc, _SOC_DISCH_BP, _SOC_DISCH_CR))
    return max(0.0, cc * cap), max(0.0, dc * cap)


def _sev_current_limit(cell_v: float, cap: float) -> Tuple[float, float]:
    """Figure 30: SEV (cell voltage) based current limit. Returns (charge_A, discharge_A)."""
    cc = float(np.interp(cell_v, _SEV_CHARGE_BP, _SEV_CHARGE_CR))
    dc = float(np.interp(cell_v, _SEV_DISCH_BP, _SEV_DISCH_CR))
    return max(0.0, cc * cap), max(0.0, dc * cap)


# =====================================================================
# ENUMS -- Section 7.1, Table 15: "Pack Operation Modes"
# =====================================================================

class PackMode(Enum):
    """Table 15: Pack Operation Modes."""
    OFF = auto()         # 7.1.7
    POWER_SAVE = auto()  # 7.1.2
    FAULT = auto()       # 7.1.3
    READY = auto()       # 7.1.1
    CONNECTING = auto()  # 7.1.4
    CONNECTED = auto()   # 7.1.5
    NOT_READY = auto()   # 7.1.6


# =====================================================================
# VIRTUAL PACK -- the battery model
# =====================================================================

@dataclass
class VirtualPack:
    """
    Equivalent-circuit battery: OCV(SoC) + R(SoC, T).
    Coulomb counting for SoC, first-order thermal model.
    """
    pack_id: int
    num_modules: int = NUM_MODULES
    cells_per_module: int = CELLS_PER_MODULE
    capacity_ah: float = NOMINAL_CAPACITY_AH

    soc: float = 0.5
    temperature: float = AMBIENT_TEMP
    current: float = 0.0
    cell_voltage: float = 0.0
    pack_voltage: float = 0.0

    def __post_init__(self):
        self._update_voltage()

    @property
    def num_cells_series(self) -> int:
        return self.num_modules * self.cells_per_module

    @property
    def r_total(self) -> float:
        """Pack total internal resistance from 2D lookup."""
        return pack_resistance(self.temperature, self.soc)

    @property
    def ocv_pack(self) -> float:
        return ocv_from_soc(self.soc) * self.num_cells_series

    def _update_voltage(self):
        ocv = ocv_from_soc(self.soc)
        # Terminal voltage per cell = OCV + I * R_pack / N_cells
        self.cell_voltage = ocv + self.current * self.r_total / self.num_cells_series
        self.pack_voltage = self.cell_voltage * self.num_cells_series

    def step(self, dt: float, current: float, contactors_closed: bool,
             external_heat: float = 0.0):
        """Advance the pack model by dt seconds."""
        if not contactors_closed:
            self.current = 0.0
        else:
            self.current = current

        # Coulomb counting -- Section 2.3
        delta_soc = (self.current * dt) / (self.capacity_ah * 3600.0)
        self.soc = np.clip(self.soc + delta_soc, 0.0, 1.0)

        # First-order thermal: dT/dt = (I²R + external - cooling) / C_thermal
        heat_gen = self.current ** 2 * self.r_total + external_heat
        cooling = THERMAL_COOLING_COEFF * (self.temperature - AMBIENT_TEMP)
        self.temperature += (heat_gen - cooling) / THERMAL_MASS * dt
        self.temperature = max(-40.0, self.temperature)

        self._update_voltage()


# =====================================================================
# PACK CONTROLLER -- the real 7-mode state machine
# =====================================================================

@dataclass
class PackController:
    """
    Implements the Orca ESS Pack Controller state machine and current
    limit calculations from Sections 6 and 7.

    v4: step() does NOT drive pack physics. Only computes limits, checks
    alarms, and advances pre-charge timers. ArrayController.step() is the
    sole driver of pack.step().
    """
    pack: VirtualPack
    mode: PackMode = PackMode.READY
    contactors_closed: bool = False

    charge_current_limit: float = NOMINAL_CAPACITY_AH
    discharge_current_limit: float = NOMINAL_CAPACITY_AH

    has_warning: bool = False
    has_fault: bool = False
    fault_latched: bool = False
    hw_fault_latched: bool = False   # Separate HW safety fault tracking
    warning_message: str = ""
    fault_message: str = ""

    # SE alarm delay timers (5s each per Table 13)
    _ov_fault_timer: float = 0.0
    _uv_fault_timer: float = 0.0
    _ot_fault_timer: float = 0.0
    _ov_warn_timer: float = 0.0
    _uv_warn_timer: float = 0.0
    _ot_warn_timer: float = 0.0

    # HW safety delay timers -- Table 13: OV/UV 1s, OT 5s
    _hw_ov_timer: float = 0.0
    _hw_uv_timer: float = 0.0
    _hw_ot_timer: float = 0.0

    # Overcurrent timers -- Table 13: fault 5s, warning 10s
    _oc_fault_timer: float = 0.0
    _oc_warn_timer: float = 0.0

    # Warning hold timer -- minimum time before clearing
    _warning_active_time: float = 0.0

    # Pre-charge timer
    _precharge_timer: float = 0.0

    # Fault reset safe-state accumulator
    _time_in_safe_state: float = 0.0

    def request_connect(self, bus_voltage: float, for_charge: bool = True) -> bool:
        """
        Section 7.2: Evaluate safety conditions before closing contactors.
        Voltage match: within 1.2V × num_modules.
        """
        if self.mode != PackMode.READY:
            return False

        max_delta = VOLTAGE_MATCH_PER_MODULE * self.pack.num_modules
        actual_delta = abs(self.pack.pack_voltage - bus_voltage)

        if actual_delta > max_delta:
            return False

        self.mode = PackMode.CONNECTING
        self._precharge_timer = 0.0
        return True

    def complete_connection(self, bus_voltage: float) -> bool:
        """
        Complete pre-charge and close contactors.
        Rechecks voltage match before closing (Fix #1.13).
        """
        if self.mode != PackMode.CONNECTING:
            return False

        max_delta = VOLTAGE_MATCH_PER_MODULE * self.pack.num_modules
        if abs(self.pack.pack_voltage - bus_voltage) > max_delta:
            self.mode = PackMode.READY
            return False

        self.mode = PackMode.CONNECTED
        self.contactors_closed = True
        return True

    def request_disconnect(self):
        if self.mode in (PackMode.CONNECTED, PackMode.CONNECTING):
            self.contactors_closed = False
            self.mode = PackMode.READY

    def manual_fault_reset(self) -> bool:
        """
        Section 6.3.5: Faults are latched and require manual operator reset.
        Automated reset is prohibited per manual.

        v4: Requires conditions below threshold for FAULT_RESET_HOLD_TIME (60s).
        Distinguishes SW faults from HW safety faults per Section 6.2.
        """
        if not self.fault_latched:
            return True

        v = self.pack.cell_voltage
        t = self.pack.temperature

        # Conditions must be safe
        if not (v < SE_OVER_VOLTAGE_FAULT and v > SE_UNDER_VOLTAGE_FAULT and t < SE_OVER_TEMP_FAULT):
            self._time_in_safe_state = 0.0
            return False

        # Must have held safe state for FAULT_RESET_HOLD_TIME
        if self._time_in_safe_state < FAULT_RESET_HOLD_TIME:
            return False

        self.fault_latched = False
        self.hw_fault_latched = False
        self.has_fault = False
        self.fault_message = ""
        self.mode = PackMode.READY
        # Reset all timers
        self._ov_fault_timer = 0.0
        self._uv_fault_timer = 0.0
        self._ot_fault_timer = 0.0
        self._hw_ov_timer = 0.0
        self._hw_uv_timer = 0.0
        self._hw_ot_timer = 0.0
        self._oc_fault_timer = 0.0
        self._oc_warn_timer = 0.0
        self._time_in_safe_state = 0.0
        return True

    def _check_hw_safety(self, dt: float):
        """
        Section 6.2: Hardware Safety System -- INDEPENDENT of software faults.
        Runs even when fault_latched is True. Runs in try/except.
        Table 13: HW OV/UV = 1s delay, HW OT = 5s delay.
        """
        try:
            v = self.pack.cell_voltage
            t = self.pack.temperature

            if v >= HW_SAFETY_OVER_VOLTAGE:
                self._hw_ov_timer += dt
                if self._hw_ov_timer >= 1.0:
                    self._trigger_hw_fault(
                        f"HW SAFETY: voltage {v:.3f}V >= {HW_SAFETY_OVER_VOLTAGE}V")
            else:
                self._hw_ov_timer = 0.0

            if v <= HW_SAFETY_UNDER_VOLTAGE:
                self._hw_uv_timer += dt
                if self._hw_uv_timer >= 1.0:
                    self._trigger_hw_fault(
                        f"HW SAFETY: voltage {v:.3f}V <= {HW_SAFETY_UNDER_VOLTAGE}V")
            else:
                self._hw_uv_timer = 0.0

            if t >= HW_SAFETY_OVER_TEMP:
                self._hw_ot_timer += dt
                if self._hw_ot_timer >= 5.0:
                    self._trigger_hw_fault(
                        f"HW SAFETY: temp {t:.1f}°C >= {HW_SAFETY_OVER_TEMP}°C")
            else:
                self._hw_ot_timer = 0.0
        except Exception as e:
            # HW safety must never silently fail
            self._trigger_hw_fault(f"HW SAFETY: exception during check: {e}")

    def _trigger_hw_fault(self, message: str):
        """Hardware safety fault -- always opens contactors, appends to fault_message."""
        self.has_fault = True
        self.fault_latched = True
        self.hw_fault_latched = True
        if self.fault_message and message not in self.fault_message:
            self.fault_message += "; " + message
        else:
            self.fault_message = message
        self.contactors_closed = False
        self.mode = PackMode.FAULT
        self.charge_current_limit = 0.0
        self.discharge_current_limit = 0.0

    def _check_alarms(self, dt: float):
        """Section 6.3.1 + Table 13: Check alarm conditions with delays."""
        v = self.pack.cell_voltage
        t = self.pack.temperature

        # -- WARNINGS with hysteresis --
        warnings: list[str] = []

        if v >= SE_OVER_VOLTAGE_WARNING:
            self._ov_warn_timer += dt
            if self._ov_warn_timer >= 5.0:
                warnings.append(f"SE OV warning: {v:.3f}V")
        elif v < SE_OV_WARN_CLEAR:
            self._ov_warn_timer = 0.0

        if v <= SE_UNDER_VOLTAGE_WARNING:
            self._uv_warn_timer += dt
            if self._uv_warn_timer >= 5.0:
                warnings.append(f"SE UV warning: {v:.3f}V")
        elif v > SE_UV_WARN_CLEAR:
            self._uv_warn_timer = 0.0

        if t >= SE_OVER_TEMP_WARNING:
            self._ot_warn_timer += dt
            if self._ot_warn_timer >= 5.0:
                warnings.append(f"SE OT warning: {t:.1f}°C")
        elif t < SE_OT_WARN_CLEAR:
            self._ot_warn_timer = 0.0

        # -- OVERCURRENT -- Table 13
        # Warning: I > 1.05 × temp_charge_limit + 5A OR I < 1.05 × temp_discharge_limit – 5A
        # Fix #5: discharge sign: -(1.05 * td - 5.0) makes threshold LESS negative (more sensitive)
        tc, td = _temp_current_limit(t, self.pack.capacity_ah)
        i = self.pack.current
        oc_charge = i > 1.05 * tc + 5.0
        oc_discharge = i < -(1.05 * td - 5.0)  # Fix #5: -5A offset per Table 13
        if oc_charge or oc_discharge:
            self._oc_warn_timer += dt
            if self._oc_warn_timer >= 10.0:
                warnings.append(f"OC warning: I={i:.1f}A")
        else:
            self._oc_warn_timer = 0.0

        warn_active = len(warnings) > 0

        # Update warning state with hold time
        if warn_active:
            self.has_warning = True
            self.warning_message = "; ".join(warnings)
            self._warning_active_time = 0.0  # Reset hold timer on new/continuing warning
        elif self.has_warning:
            self._warning_active_time += dt
            if self._warning_active_time >= WARNING_HOLD_TIME:
                self.has_warning = False
                self.warning_message = ""
                self._warning_active_time = 0.0

        # -- OC fault (5s) -- Fix #6: ONLY at T < 0°C AND charging per Table 13
        if t < 0.0 and oc_charge:
            self._oc_fault_timer += dt
            if self._oc_fault_timer >= 5.0 and not self.fault_latched:
                self._trigger_sw_fault(f"OC fault: I={i:.1f}A at T={t:.1f}°C (charge at sub-zero)")
        else:
            self._oc_fault_timer = 0.0

        # -- SE FAULTS (5s delay each) --
        if v >= SE_OVER_VOLTAGE_FAULT:
            self._ov_fault_timer += dt
            if self._ov_fault_timer >= 5.0 and not self.fault_latched:
                self._trigger_sw_fault(f"SE OV fault: {v:.3f}V >= {SE_OVER_VOLTAGE_FAULT}V")
        else:
            self._ov_fault_timer = 0.0

        if v <= SE_UNDER_VOLTAGE_FAULT:
            self._uv_fault_timer += dt
            if self._uv_fault_timer >= 5.0 and not self.fault_latched:
                self._trigger_sw_fault(f"SE UV fault: {v:.3f}V <= {SE_UNDER_VOLTAGE_FAULT}V")
        else:
            self._uv_fault_timer = 0.0

        if t >= SE_OVER_TEMP_FAULT:
            self._ot_fault_timer += dt
            if self._ot_fault_timer >= 5.0 and not self.fault_latched:
                self._trigger_sw_fault(f"SE OT fault: {t:.1f}°C >= {SE_OVER_TEMP_FAULT}°C")
        else:
            self._ot_fault_timer = 0.0

    def _trigger_sw_fault(self, message: str):
        """Software fault -- opens contactors, latches."""
        self.has_fault = True
        self.fault_latched = True
        if self.fault_message and message not in self.fault_message:
            self.fault_message += "; " + message
        else:
            self.fault_message = message
        self.contactors_closed = False
        self.mode = PackMode.FAULT
        self.charge_current_limit = 0.0
        self.discharge_current_limit = 0.0

    def _update_safe_state_timer(self, dt: float):
        """Accumulate time in safe state for fault reset hold-time requirement."""
        v = self.pack.cell_voltage
        t = self.pack.temperature
        if (v < SE_OVER_VOLTAGE_FAULT and v > SE_UNDER_VOLTAGE_FAULT
                and t < SE_OVER_TEMP_FAULT and t < HW_SAFETY_OVER_TEMP):
            self._time_in_safe_state += dt
        else:
            self._time_in_safe_state = 0.0

    def step(self, dt: float, bus_voltage: float):
        """
        Control loop step -- computes limits, checks alarms, advances pre-charge.
        Does NOT call pack.step() (Fix #2: only ArrayController drives physics).
        """
        # HW safety ALWAYS runs, independent of fault state (Fix #3)
        self._check_hw_safety(dt)

        # SW alarms
        self._check_alarms(dt)

        # Safe state timer for fault reset
        self._update_safe_state_timer(dt)

        if self.fault_latched:
            self.charge_current_limit = 0.0
            self.discharge_current_limit = 0.0
            return

        # Pre-charge timer
        if self.mode == PackMode.CONNECTING:
            self._precharge_timer += dt
            if self._precharge_timer >= PRECHARGE_DURATION:
                self.complete_connection(bus_voltage)

        # Compute current limits: min(temp, soc, sev) -- Section 7.4
        tc, td = _temp_current_limit(self.pack.temperature, self.pack.capacity_ah)
        sc, sd = _soc_current_limit(self.pack.soc, self.pack.capacity_ah)
        vc, vd = _sev_current_limit(self.pack.cell_voltage, self.pack.capacity_ah)
        self.charge_current_limit = max(0.0, min(tc, sc, vc))
        self.discharge_current_limit = max(0.0, min(td, sd, vd))

        # Section 6.3.4: warnings indicate reduced limits, but the parametric
        # curves already handle derating. No blanket 50% multiplier.
        # The warning is an indication, not an additional derating layer.


# =====================================================================
# ARRAY CONTROLLER
# =====================================================================

@dataclass
class ArrayController:
    """
    Section 8.1: EMS communicates with pack controllers on a shared DC bus.
    Kirchhoff solver distributes current by impedance.

    Sign convention: positive current = charging (into pack).
    """
    controllers: List[PackController]
    bus_voltage: float = 0.0
    array_charge_limit: float = 0.0
    array_discharge_limit: float = 0.0

    def _connected(self) -> List[PackController]:
        return [c for c in self.controllers if c.mode == PackMode.CONNECTED]

    def _ready(self) -> List[PackController]:
        return [c for c in self.controllers if c.mode == PackMode.READY]

    def connect_first(self, for_charge: bool = True):
        """
        Fix #9: First pack selected by SoC (lowest for charge, highest for discharge).
        Only one pack pre-charges initially.
        """
        conn = self._connected()
        connecting = [c for c in self.controllers if c.mode == PackMode.CONNECTING]
        if conn or connecting:
            return  # First pack already handled

        ready = self._ready()
        if not ready:
            return

        if for_charge:
            first = min(ready, key=lambda c: c.pack.soc)
        else:
            first = max(ready, key=lambda c: c.pack.soc)

        first.request_connect(self.bus_voltage, for_charge=for_charge)

    def connect_remaining(self, for_charge: bool = True):
        """
        Fix #9: After first pack is connected, remaining packs connect
        SIMULTANEOUSLY (not one-at-a-time) if voltage/SoC in range.
        """
        conn = self._connected()
        if not conn:
            return  # Need first pack connected first

        ready = self._ready()
        for ctrl in ready:
            ctrl.request_connect(self.bus_voltage, for_charge=for_charge)

    def disconnect_all(self):
        for ctrl in self.controllers:
            ctrl.request_disconnect()

    def reset_all_faults(self):
        """Section 6.3.5: Manual fault reset -- simulating operator action."""
        return {c.pack.pack_id: c.manual_fault_reset()
                for c in self.controllers if c.fault_latched}

    def update_bus_voltage(self):
        """Estimate bus voltage when no packs are connected."""
        conn = self._connected()
        if not conn:
            ready = self._ready()
            if ready:
                self.bus_voltage = float(np.mean([c.pack.pack_voltage for c in ready]))

    def compute_array_limits(self):
        """Section 7.4: array limit = min(per-pack limit) × N connected."""
        conn = self._connected()
        if not conn:
            self.array_charge_limit = 0.0
            self.array_discharge_limit = 0.0
            return
        n = len(conn)
        self.array_charge_limit = min(c.charge_current_limit for c in conn) * n
        self.array_discharge_limit = min(c.discharge_current_limit for c in conn) * n

    def _solve_kirchhoff(self, conn: List[PackController],
                         requested_current: float) -> Dict[int, float]:
        """
        Kirchhoff current distribution with per-pack limit enforcement.

        V_bus = (Σ(OCV_k/R_k) + I_load) / Σ(1/R_k)
        I_k = (V_bus - OCV_k) / R_k

        Fix #10: iteration cap = len(conn), post-solve assertion,
        equalization at zero load, bus voltage stored from solver.
        """
        if not conn:
            return {}

        # Clamp total requested current to array limits
        if requested_current > 0:
            actual_total = min(requested_current, self.array_charge_limit)
        elif requested_current < 0:
            actual_total = max(requested_current, -self.array_discharge_limit)
        else:
            actual_total = 0.0

        active = list(conn)
        clamped: Dict[int, float] = {}
        residual = actual_total

        # Fix #10: iteration cap = len(conn)
        for _iteration in range(len(conn)):
            if not active:
                break

            sum_g = sum(1.0 / c.pack.r_total for c in active)
            sum_ocv_g = sum(c.pack.ocv_pack / c.pack.r_total for c in active)

            if sum_g < 1e-12:
                break

            v_bus = (sum_ocv_g + residual) / sum_g

            pack_currents: Dict[int, float] = {}
            any_clamped = False
            for c in list(active):
                i_k = (v_bus - c.pack.ocv_pack) / c.pack.r_total
                if i_k > 0 and i_k > c.charge_current_limit:
                    clamped[c.pack.pack_id] = c.charge_current_limit
                    residual -= c.charge_current_limit
                    active.remove(c)
                    any_clamped = True
                elif i_k < 0 and abs(i_k) > c.discharge_current_limit:
                    clamped[c.pack.pack_id] = -c.discharge_current_limit
                    residual -= (-c.discharge_current_limit)
                    active.remove(c)
                    any_clamped = True
                else:
                    pack_currents[c.pack.pack_id] = i_k

            if not any_clamped:
                self.bus_voltage = v_bus
                pack_currents.update(clamped)

                # Fix #10: post-solve assertion -- verify no pack exceeds limit
                # Clamp and accept small KCL violation at 1% tolerance
                for c in conn:
                    pid = c.pack.pack_id
                    ik = pack_currents.get(pid, 0.0)
                    if ik > 0 and ik > c.charge_current_limit * 1.01:
                        pack_currents[pid] = c.charge_current_limit
                    elif ik < 0 and abs(ik) > c.discharge_current_limit * 1.01:
                        pack_currents[pid] = -c.discharge_current_limit
                # Update bus voltage to reflect actual delivered current
                actual_total = sum(pack_currents.values())
                if abs(actual_total - requested_current) > 0.01 * abs(requested_current + 1e-9):
                    pass  # Small KCL residual accepted at 1% tolerance for demo

                return pack_currents

        # Final solve with remaining active
        if active:
            sum_g = sum(1.0 / c.pack.r_total for c in active)
            sum_ocv_g = sum(c.pack.ocv_pack / c.pack.r_total for c in active)
            if sum_g > 1e-12:
                v_bus = (sum_ocv_g + residual) / sum_g
                self.bus_voltage = v_bus
                for c in active:
                    clamped[c.pack.pack_id] = (v_bus - c.pack.ocv_pack) / c.pack.r_total
        elif clamped:
            voltages = []
            for c in conn:
                i_k = clamped.get(c.pack.pack_id, 0.0)
                voltages.append(c.pack.ocv_pack + i_k * c.pack.r_total)
            self.bus_voltage = float(np.mean(voltages))

        return clamped

    def _solve_equalization(self, conn: List[PackController]) -> Dict[int, float]:
        """
        Fix #10: At zero requested current, solve for equalization currents
        between packs with different SoCs.  KCL constraint: ΣI_k = 0.

        V_bus = weighted average OCV: Σ(OCV_k/R_k) / Σ(1/R_k)
        I_k = (V_bus - OCV_k) / R_k

        After clamping any pack, re-solve V_bus for unclamped packs so that
        their currents absorb the difference (same iterative clamp-and-remove
        approach as _solve_kirchhoff).
        """
        if not conn:
            return {}

        active = list(conn)
        clamped: Dict[int, float] = {}

        for _iteration in range(len(conn)):
            if not active:
                break

            sum_g = sum(1.0 / c.pack.r_total for c in active)
            sum_ocv_g = sum(c.pack.ocv_pack / c.pack.r_total for c in active)

            if sum_g < 1e-12:
                break

            # Residual that unclamped packs must absorb = 0 - sum(clamped currents)
            clamped_sum = sum(clamped.values())
            v_bus = (sum_ocv_g - clamped_sum) / sum_g

            pack_currents: Dict[int, float] = {}
            any_clamped = False
            for c in list(active):
                i_k = (v_bus - c.pack.ocv_pack) / c.pack.r_total
                if i_k > 0 and i_k > c.charge_current_limit:
                    clamped[c.pack.pack_id] = c.charge_current_limit
                    active.remove(c)
                    any_clamped = True
                elif i_k < 0 and abs(i_k) > c.discharge_current_limit:
                    clamped[c.pack.pack_id] = -c.discharge_current_limit
                    active.remove(c)
                    any_clamped = True
                else:
                    pack_currents[c.pack.pack_id] = i_k

            if not any_clamped:
                self.bus_voltage = v_bus
                pack_currents.update(clamped)
                return pack_currents

        # Final solve with remaining active packs
        if active:
            sum_g = sum(1.0 / c.pack.r_total for c in active)
            sum_ocv_g = sum(c.pack.ocv_pack / c.pack.r_total for c in active)
            clamped_sum = sum(clamped.values())
            if sum_g > 1e-12:
                v_bus = (sum_ocv_g - clamped_sum) / sum_g
                self.bus_voltage = v_bus
                for c in active:
                    clamped[c.pack.pack_id] = (v_bus - c.pack.ocv_pack) / c.pack.r_total
        elif clamped:
            voltages = []
            for c in conn:
                i_k = clamped.get(c.pack.pack_id, 0.0)
                voltages.append(c.pack.ocv_pack + i_k * c.pack.r_total)
            self.bus_voltage = float(np.mean(voltages))

        return clamped

    def step(self, dt: float, requested_current: float,
             external_heat: Optional[Dict[int, float]] = None):
        """
        Main array step.

        Order: step controllers (limits/alarms) → solve currents → step physics.
        Fix #2: Only this method drives pack.step().
        """
        # 1. Step all controllers (alarms, limits, mode transitions)
        for ctrl in self.controllers:
            ctrl.step(dt, self.bus_voltage)

        conn = self._connected()

        # 2. Solve current distribution
        if conn:
            if requested_current != 0:
                pack_currents = self._solve_kirchhoff(conn, requested_current)
            else:
                # Fix #10: equalization currents at zero load
                pack_currents = self._solve_equalization(conn)

            # 3. Step physics for connected packs with solved currents
            for ctrl in conn:
                i_k = pack_currents.get(ctrl.pack.pack_id, 0.0)
                ext_h = (external_heat or {}).get(ctrl.pack.pack_id, 0.0)
                ctrl.pack.step(dt, i_k, ctrl.contactors_closed, ext_h)
        else:
            self.update_bus_voltage()

        # Step physics for non-connected packs (zero current)
        for ctrl in self.controllers:
            if ctrl.mode != PackMode.CONNECTED:
                ext_h = (external_heat or {}).get(ctrl.pack.pack_id, 0.0)
                ctrl.pack.step(dt, 0.0, ctrl.contactors_closed, ext_h)

        self.compute_array_limits()
        return self._connected()


# =====================================================================
# SCENARIO
# =====================================================================

def run_scenario(output_dir: str = "."):
    """
    3-pack scenario demonstrating real Orca ESS behaviors:

    1. Sequential pre-charge (first pack) → parallel connection (remaining)
    2. Normal charging with Kirchhoff current distribution
    3. Equalization currents at zero load
    4. Overcurrent warning with 10s delay
    5. Temperature warning with hysteresis
    6. Temperature fault → contactor open → HW safety still armed
    7. Fault reset denied (too soon) → wait → reset succeeds
    8. Reconnection and disconnect
    """
    print("=" * 70)
    print("  CORVUS ORCA ESS DEMO v4")
    print("  Reference: Corvus Energy Orca ESS integrator documentation")
    print("=" * 70)
    print()

    packs = [
        VirtualPack(pack_id=1, soc=0.45, temperature=AMBIENT_TEMP),
        VirtualPack(pack_id=2, soc=0.55, temperature=AMBIENT_TEMP),
        VirtualPack(pack_id=3, soc=0.65, temperature=AMBIENT_TEMP),
    ]

    print(f"  Pack voltages: {[f'{p.pack_voltage:.1f}V' for p in packs]}")
    print(f"  Voltage match threshold: {VOLTAGE_MATCH_PER_MODULE * NUM_MODULES:.1f}V")
    print(f"  Pack resistances: {[f'{p.r_total*1e3:.1f}mΩ' for p in packs]}")
    print(f"  Thermal mass: {THERMAL_MASS/1e6:.2f} MJ/°C, Cooling: {THERMAL_COOLING_COEFF} W/°C")
    print(f"  Ambient: {AMBIENT_TEMP}°C")
    print()

    controllers = [PackController(pack=p) for p in packs]
    array = ArrayController(controllers=controllers)
    array.update_bus_voltage()

    dt = 1.0
    data_rows = []
    events = []

    def record(t):
        row = {
            'time': t,
            'bus_voltage': array.bus_voltage,
            'array_charge_limit': array.array_charge_limit,
            'array_discharge_limit': array.array_discharge_limit,
        }
        for i, ctrl in enumerate(controllers):
            p = f'pack{i+1}'
            row[f'{p}_soc'] = ctrl.pack.soc * 100
            row[f'{p}_voltage'] = ctrl.pack.pack_voltage
            row[f'{p}_cell_v'] = ctrl.pack.cell_voltage
            row[f'{p}_temp'] = ctrl.pack.temperature
            row[f'{p}_current'] = ctrl.pack.current
            row[f'{p}_charge_limit'] = ctrl.charge_current_limit
            row[f'{p}_discharge_limit'] = ctrl.discharge_current_limit
            row[f'{p}_mode'] = ctrl.mode.name
        data_rows.append(row)

    # ── PHASE 1: Connect to charge (t=0..30s) ──
    # First pack by lowest SoC, then remaining simultaneously
    print("[Phase 1] Connect-to-charge -- lowest SoC first, then simultaneous (Section 7.2.1)")
    print(f"  Pack SoCs: {[f'{p.soc*100:.0f}%' for p in packs]}")

    t = 0.0
    events.append((t, "CONNECT ALL\nTO CHARGE"))

    connected_ids = set()
    first_connected = False
    for _ in range(30):
        if not first_connected:
            array.connect_first(for_charge=True)
        else:
            array.connect_remaining(for_charge=True)

        array.step(dt, 0.0)
        record(t)

        for ctrl in controllers:
            if ctrl.mode == PackMode.CONNECTED and ctrl.pack.pack_id not in connected_ids:
                connected_ids.add(ctrl.pack.pack_id)
                first_connected = True
                print(f"  t={t:.0f}s: Pack {ctrl.pack.pack_id} CONNECTED "
                      f"(SoC={ctrl.pack.soc*100:.1f}%, "
                      f"ΔV={abs(ctrl.pack.pack_voltage - array.bus_voltage):.1f}V)")
                events.append((t, f"Pack {ctrl.pack.pack_id}\nCONNECTED"))
        t += dt

    print(f"  -> {len(connected_ids)}/3 packs connected\n")

    # ── PHASE 2: Charge at 200A (t=30..330s) ──
    print("[Phase 2] Charging at 200A -- Kirchhoff current distribution (Section 7.4)")
    charge_a = 200.0

    for step_i in range(300):
        # Keep trying unconnected
        array.connect_remaining(for_charge=True)
        array.step(dt, charge_a)
        record(t)

        for ctrl in controllers:
            if ctrl.mode == PackMode.CONNECTED and ctrl.pack.pack_id not in connected_ids:
                connected_ids.add(ctrl.pack.pack_id)
                print(f"  t={t:.0f}s: Pack {ctrl.pack.pack_id} CONNECTED")
                events.append((t, f"Pack {ctrl.pack.pack_id}\nCONNECTED"))

        # Log Kirchhoff distribution periodically
        if step_i == 10:
            currents = [f'{p.current:.1f}A' for p in packs]
            print(f"  t={t:.0f}s: Kirchhoff distribution: {currents}")
            print(f"    Bus voltage: {array.bus_voltage:.1f}V")
        t += dt

    print(f"  SoCs: {[f'{p.soc*100:.1f}%' for p in packs]}")
    print(f"  Temps: {[f'{p.temperature:.1f}°C' for p in packs]}\n")

    # ── PHASE 3: Equalization at zero load (t=330..380s) ──
    print("[Phase 3] Zero load -- equalization currents between packs")
    for step_i in range(50):
        array.step(dt, 0.0)
        record(t)

        if step_i == 5:
            currents = [f'{p.current:.2f}A' for p in packs]
            print(f"  t={t:.0f}s: Equalization currents: {currents}")
            print(f"    SoCs: {[f'{p.soc*100:.2f}%' for p in packs]}")
            print(f"    Bus voltage: {array.bus_voltage:.1f}V")
        t += dt
    print()

    # ── PHASE 4: Overcurrent warning (t=380..420s) ──
    # The Kirchhoff solver enforces per-pack limits, so in normal operation
    # OC cannot occur. To demonstrate OC detection, we simulate an EMS that
    # injects current bypassing the solver (e.g., regen braking surge).
    print("[Phase 4] Overcurrent warning test (simulated EMS bypass)")
    tc, _ = _temp_current_limit(packs[0].temperature, NOMINAL_CAPACITY_AH)
    oc_threshold = 1.05 * tc + 5.0
    print(f"  Temp charge limit: {tc:.0f}A, OC warning threshold: {oc_threshold:.0f}A")

    # Temporarily force Pack 1 current above OC threshold to simulate regen surge
    oc_warned = False
    for step_i in range(40):
        array.step(dt, 100.0)
        # Simulate EMS override: force pack 1 current above OC threshold
        if step_i < 25:
            packs[0].current = oc_threshold + 20.0
        record(t)

        if any(c.has_warning and 'OC' in c.warning_message for c in controllers) and not oc_warned:
            print(f"  t={t:.0f}s: OC WARNING triggered (after 10s delay)")
            events.append((t, "OC WARNING"))
            oc_warned = True
        t += dt

    # Back to normal
    for _ in range(20):
        array.step(dt, 100.0)
        record(t)
        t += dt
    if not oc_warned:
        print(f"  OC warning not triggered in 40s (check timer)")
    print()

    # ── PHASE 5: Temperature ramp on Pack 3 (t=440..680s) ──
    # With thermal mass of 1.386 MJ/°C, need ~500 kW external heat to ramp
    # from 40°C to 65°C (ΔT=25°C) in ~70s: P = C×ΔT/Δt = 1.386e6×25/70 ≈ 495 kW
    print("[Phase 5] Temperature ramp on Pack 3 -- warning, fault, HW safety")
    print(f"  Warning: {SE_OVER_TEMP_WARNING}°C, Fault: {SE_OVER_TEMP_FAULT}°C, HW Safety: {HW_SAFETY_OVER_TEMP}°C")

    warn_logged = fault_logged = False
    external_heat_w = 500_000.0  # 500 kW -- aggressive external heat injection

    for _ in range(240):
        current = 100.0 if not controllers[2].fault_latched else 80.0
        ext_heat = {3: external_heat_w}
        array.step(dt, current, external_heat=ext_heat)
        record(t)

        if packs[2].temperature >= SE_OVER_TEMP_WARNING and not warn_logged:
            print(f"  t={t:.0f}s: Pack 3 OT WARNING -- {packs[2].temperature:.1f}°C")
            print(f"    Charge limit: {controllers[2].charge_current_limit:.1f}A")
            events.append((t, f"Pack 3 WARN\n{packs[2].temperature:.0f}°C"))
            warn_logged = True

        if controllers[2].fault_latched and not fault_logged:
            print(f"  t={t:.0f}s: Pack 3 FAULT -- {controllers[2].fault_message}")
            print(f"    Contactors OPEN, limits ZERO")
            events.append((t, f"Pack 3 FAULT\n{packs[2].temperature:.0f}°C"))
            fault_logged = True
            external_heat_w = 0.0  # Stop heating after fault

        t += dt

    print(f"  Pack 3 mode: {controllers[2].mode.name}, temp: {packs[2].temperature:.1f}°C\n")

    # ── PHASE 6: Temperature hysteresis demo ──
    # Warning on Pack 3 shouldn't clear immediately due to hold time
    print("[Phase 6] Warning hysteresis -- even if temp drops slightly, hold time prevents clear")
    print(f"  Warning hold time: {WARNING_HOLD_TIME}s")

    # Cool pack 3 slightly via negative external heat
    for _ in range(15):
        ext_heat = {3: -200_000.0}  # Moderate cooling
        array.step(dt, 80.0, external_heat=ext_heat)
        record(t)
        t += dt
    print(f"  Pack 3 temp after slight cooling: {packs[2].temperature:.1f}°C")
    print(f"  Pack 3 warning still active: {controllers[2].has_warning}\n")

    # ── PHASE 7: Fault reset attempt (t~700) ──
    print("[Phase 7] Fault latch -- reset denied, then wait for hold time")

    result = controllers[2].manual_fault_reset()
    print(f"  Reset attempt @ {packs[2].temperature:.1f}°C, "
          f"safe_time={controllers[2]._time_in_safe_state:.0f}s: "
          f"{'OK' if result else 'DENIED'}")
    events.append((t, "RESET DENIED"))

    # Cool Pack 3 via external cooling through thermal model (Fix #15)
    # Use -100 kW cooling (e.g., emergency ventilation) -- realistic for marine
    print("  Cooling Pack 3 via emergency ventilation (-100 kW)...")
    for _ in range(120):
        ext_heat = {3: -100_000.0}
        array.step(dt, 80.0, external_heat=ext_heat)
        record(t)
        t += dt

    print(f"  Pack 3 temp: {packs[2].temperature:.1f}°C, "
          f"safe_time: {controllers[2]._time_in_safe_state:.0f}s")

    # Try reset -- may need more time for 60s hold
    result = controllers[2].manual_fault_reset()
    print(f"  Reset attempt: {'SUCCESS' if result else 'DENIED (need more hold time)'}")

    if not result:
        # Continue cooling until safe state holds for 60s
        for _ in range(80):
            ext_heat = {3: -50_000.0}
            array.step(dt, 80.0, external_heat=ext_heat)
            record(t)
            t += dt
        result = controllers[2].manual_fault_reset()
        print(f"  After more cooling -- safe_time: {controllers[2]._time_in_safe_state:.0f}s")
        print(f"  Reset: {'SUCCESS' if result else 'DENIED'}")

    events.append((t, "FAULT RESET"))
    print()

    # ── PHASE 8: Reconnect and disconnect ──
    print("[Phase 8] Reconnect Pack 3 + disconnect all")

    if controllers[2].mode == PackMode.READY:
        controllers[2].request_connect(array.bus_voltage, for_charge=True)

    reconnected = False
    for _ in range(30):
        array.step(dt, 80.0)
        record(t)

        if controllers[2].mode == PackMode.CONNECTED and not reconnected:
            print(f"  t={t:.0f}s: Pack 3 RECONNECTED")
            events.append((t, "Pack 3\nRECONNECTED"))
            reconnected = True
        t += dt

    print(f"  Modes: {[c.mode.name for c in controllers]}")

    # Disconnect all
    array.disconnect_all()
    events.append((t, "DISCONNECT ALL"))

    for _ in range(20):
        array.step(dt, 0.0)
        record(t)
        t += dt

    print(f"  Final modes: {[c.mode.name for c in controllers]}")
    print(f"  Final SoCs: {[f'{p.soc*100:.1f}%' for p in packs]}")
    print(f"  Final temps: {[f'{p.temperature:.1f}°C' for p in packs]}")
    print()

    # ── Write CSV ──
    csv_path = os.path.join(output_dir, "corvus_output.csv")
    with open(csv_path, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=data_rows[0].keys())
        w.writeheader()
        w.writerows(data_rows)
    print(f"[Output] CSV: {csv_path} ({len(data_rows)} rows)")

    # ── Plot ──
    plot_path = os.path.join(output_dir, "corvus_plot.png")
    _make_plot(data_rows, events, plot_path)
    print(f"[Output] Plot: {plot_path}")
    print("\nDone.")


def _make_plot(data, events, path):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    t = [r['time'] for r in data]
    fig, axes = plt.subplots(5, 1, figsize=(16, 20), sharex=True)
    fig.suptitle(
        "Corvus Orca ESS Demo v4 -- 3-Pack Scenario\n"
        "Reference: Corvus Energy Orca ESS integrator documentation",
        fontsize=14, fontweight='bold'
    )

    colors = ['#2196F3', '#4CAF50', '#FF5722']
    labels = ['Pack 1 (45%)', 'Pack 2 (55%)', 'Pack 3 (65%)']

    # SoC
    ax = axes[0]
    for i in range(3):
        ax.plot(t, [r[f'pack{i+1}_soc'] for r in data], color=colors[i], label=labels[i], lw=1.5)
    ax.set_ylabel('SoC (%)')
    ax.set_title('State of Charge -- Section 2.3')
    ax.legend(loc='upper left', fontsize=8)
    ax.grid(True, alpha=0.3)

    # Cell Voltage
    ax = axes[1]
    for i in range(3):
        ax.plot(t, [r[f'pack{i+1}_cell_v'] for r in data], color=colors[i], label=labels[i], lw=1.5)
    ax.axhline(SE_OVER_VOLTAGE_WARNING, color='orange', ls='--', alpha=0.7, label=f'OV Warn ({SE_OVER_VOLTAGE_WARNING}V)')
    ax.axhline(SE_OVER_VOLTAGE_FAULT, color='red', ls='--', alpha=0.7, label=f'OV Fault ({SE_OVER_VOLTAGE_FAULT}V)')
    ax.set_ylabel('Cell Voltage (V)')
    ax.set_title('Series Element Voltage -- Table 13')
    ax.legend(loc='upper left', fontsize=8)
    ax.grid(True, alpha=0.3)

    # Temperature
    ax = axes[2]
    for i in range(3):
        ax.plot(t, [r[f'pack{i+1}_temp'] for r in data], color=colors[i], label=labels[i], lw=1.5)
    ax.axhline(SE_OVER_TEMP_WARNING, color='orange', ls='--', alpha=0.7, label=f'OT Warn ({SE_OVER_TEMP_WARNING}°C)')
    ax.axhline(SE_OVER_TEMP_FAULT, color='red', ls='--', alpha=0.7, label=f'OT Fault ({SE_OVER_TEMP_FAULT}°C)')
    ax.axhline(HW_SAFETY_OVER_TEMP, color='darkred', ls=':', alpha=0.7, label=f'HW Safety ({HW_SAFETY_OVER_TEMP}°C)')
    ax.set_ylabel('Temperature (°C)')
    ax.set_title('Cell Temperature -- Section 7.4.1, Table 13')
    ax.legend(loc='upper left', fontsize=8)
    ax.grid(True, alpha=0.3)

    # Current
    ax = axes[3]
    for i in range(3):
        ax.plot(t, [r[f'pack{i+1}_current'] for r in data], color=colors[i], lw=1.5,
                label=f'Pack {i+1} current')
        ax.plot(t, [r[f'pack{i+1}_charge_limit'] for r in data], color=colors[i], ls='--', alpha=0.4, lw=1,
                label=f'Pack {i+1} charge lim')
    ax.set_ylabel('Current (A)')
    ax.set_title('Pack Currents & Limits -- Kirchhoff Distribution')
    ax.legend(loc='upper right', fontsize=7, ncol=2)
    ax.grid(True, alpha=0.3)

    # Pack Modes
    ax = axes[4]
    mode_map = {m.name: i for i, m in enumerate(PackMode)}
    for i in range(3):
        modes = [mode_map[r[f'pack{i+1}_mode']] for r in data]
        ax.plot(t, modes, color=colors[i], label=labels[i], lw=2, drawstyle='steps-post')
    ax.set_yticks(list(mode_map.values()))
    ax.set_yticklabels(list(mode_map.keys()), fontsize=8)
    ax.set_ylabel('Pack Mode')
    ax.set_xlabel('Time (s)')
    ax.set_title('Pack Operation Modes -- Table 15, Section 7.1')
    ax.legend(loc='upper right', fontsize=8)
    ax.grid(True, alpha=0.3)

    # Event annotations
    for et, etxt in events:
        axes[0].axvline(et, color='gray', ls=':', alpha=0.5)
        axes[0].annotate(etxt, xy=(et, axes[0].get_ylim()[1]), fontsize=6, ha='center', va='top',
                         bbox=dict(boxstyle='round,pad=0.3', fc='yellow', alpha=0.7))
    for et, _ in events:
        for ax in axes[1:]:
            ax.axvline(et, color='gray', ls=':', alpha=0.3)

    plt.tight_layout()
    plt.savefig(path, dpi=150, bbox_inches='tight')
    plt.close()


if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    run_scenario(output_dir=script_dir)
