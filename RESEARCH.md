# Corvus Orca ESS Simulation Parameters — Research

*Generated 2026-02-27 by research agent*

---

## 1. NMC Pouch Cell Internal Resistance

### Source Data
- **Corvus Integrator Manual p.32**: "The resistance of **3.3 mΩ/module** is primarily due to the internal resistance of the NMC cells, and the 500nH/module is stray inductance from the battery pack power path."
- Module = 128 Ah, ~50 VDC (approximately 14 cells in series based on 3.6V nominal)

### Derived Values
- **Per-module DC resistance: 3.3 mΩ** (from manufacturer — this is the gold standard value)
- Per-cell estimate: 3.3 mΩ / 14s ≈ 0.24 mΩ per cell (consistent with large-format NMC pouch cells)
- For a 22-module pack in series: **R_pack = 22 × 3.3 mΩ = 72.6 mΩ**

### R vs Temperature & SoC (Literature-Based Estimates)
Large NMC pouch cells typically show:
- **Temperature dependence**: R roughly doubles from 25°C to 0°C, and triples at -10°C
- **SoC dependence**: R is ~20-40% higher at very low SoC (<10%) and very high SoC (>95%), relatively flat 20-80%

#### Recommended Lookup Table: R_module(T, SoC) in mΩ

| SoC \ Temp | -10°C | 0°C  | 10°C | 25°C | 35°C | 45°C |
|------------|-------|------|------|------|------|------|
| 5%         | 15.3  | 9.7  | 6.2  | 5.0  | 4.4  | 4.1  |
| 20%        | 10.9  | 7.2  | 4.7  | 3.6  | 3.3  | 3.1  |
| 35%        | 9.9   | 6.6  | 4.3  | 3.3  | 3.0  | 2.8  |
| 50%        | 9.3   | 6.2  | 4.0  | 3.1  | 2.8  | 2.6  |
| 65%        | 9.6   | 6.4  | 4.2  | 3.2  | 2.9  | 2.7  |
| 80%        | 10.2  | 6.8  | 4.4  | 3.4  | 3.1  | 2.9  |
| 95%        | 13.5  | 8.9  | 5.6  | 4.2  | 3.9  | 3.6  |

**Note**: The impedance vs SoC follows a U-shape — minimum around 40-60% SoC due to charge-transfer resistance minimum at mid-lithiation, higher at extremes (depleted anode at low SoC, increased charge-transfer resistance at nearly-full cathode).

**Confidence: HIGH** for the 25°C/mid-SoC value (manufacturer data). MEDIUM for the temperature/SoC variation (based on published NMC pouch cell literature patterns scaled to the known 3.3 mΩ baseline).

### Recommended Values for Demo
```python
R_MODULE_NOMINAL = 3.3e-3  # Ω, at 25°C, mid-SoC
R_PACK_NOMINAL = 72.6e-3   # Ω, 22 modules in series
```

---

## 2. Pack Thermal Mass and Cooling

### Source Data
- **Module weight: 60 kg** (from Corvus Orca datasheet)
- **Pack: 22 modules** → total module mass ≈ 1320 kg (+ rack/BMS/cabling ~200 kg → ~1520 kg total)
- NMC cell specific heat capacity: **~1000-1100 J/(kg·K)** (widely reported in literature for NMC pouch cells)
- Pack-level effective specific heat (including aluminum, copper, plastics): **~800-900 J/(kg·K)**

### Thermal Mass Calculation
- Cell mass dominates (~70% of module mass): 60 kg × 0.7 = 42 kg cells per module
- Total cell mass: 22 × 42 = 924 kg
- Non-cell mass: 22 × 18 + 200 = 596 kg (specific heat ~500 J/(kg·K) for metals)
- **THERMAL_MASS ≈ 924 × 1050 + 596 × 500 ≈ 1,268,200 J/°C ≈ 1.27 MJ/°C**

### Cooling
- Corvus uses forced-air cooling: cool air enters at rack base, flows upward between modules and rack back panel
- Typical forced-air battery cooling: **thermal resistance ≈ 0.01-0.05 °C/W** depending on airflow
- For a pack dissipating ~10-50 kW at full load, ΔT of 10-20°C is typical
- **Effective cooling coefficient: ~500-2000 W/°C** (depends on fan speed)

### Recommended Values for Demo
```python
THERMAL_MASS = 1_268_000    # J/°C (1.27 MJ/°C) for full pack
THERMAL_COOLING_COEFF = 800  # W/°C (forced air, moderate airflow)
AMBIENT_TEMP = 40.0          # °C -- realistic engine room
```

**Confidence: MEDIUM** — cell specific heat is well-established, but exact pack composition and airflow characteristics are estimated.

---

## 3. OCV Curve for NMC Cells

### Source
Based on published NMC 622/graphite OCV curves (multiple sources: Journal of The Electrochemical Society, Battery University, cell manufacturer datasheets). NMC 622 nominal voltage: 3.6-3.65V, range 2.5-4.2V.

### High-Resolution OCV vs SoC Table (NMC 622 / Graphite)

```python
# OCV lookup table for single NMC 622 cell
SOC_BREAKPOINTS = [
    0.00, 0.02, 0.05, 0.08, 0.10, 0.15, 0.20, 0.25,
    0.30, 0.35, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65,
    0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 0.98, 1.00
]

OCV_VALUES = [  # Volts per cell
    3.000, 3.280, 3.420, 3.480, 3.510, 3.555, 3.590, 3.610,
    3.625, 3.638, 3.650, 3.662, 3.675, 3.690, 3.710, 3.735,
    3.765, 3.800, 3.845, 3.900, 3.960, 4.030, 4.100, 4.190
]

# For a 14s module (50V nominal):
# OCV_module = OCV_cell * 14
# For a 22-module pack:
# OCV_pack = OCV_module * 22 = OCV_cell * 308
```

### Voltage Range for 308s Pack
| SoC  | Cell V | Pack V  |
|------|--------|---------|
| 0%   | 3.000  | 924 V   |
| 10%  | 3.510  | 1081 V  |
| 50%  | 3.675  | 1132 V  |
| 90%  | 3.960  | 1220 V  |
| 100% | 4.190  | 1291 V  |

**Confidence: HIGH** — NMC 622 OCV curves are well-characterized in literature. Exact cell-to-cell variation is small (~±5 mV).

---

## 4. Current Limit Curves (derived from integrator documentation)

### Temperature-Based Current Limit (derating curve from integrator documentation)

**Charge Limit (positive C-rate):**

| Temp (°C) | C-rate |
|-----------|--------|
| -25       | 0.0    |
| 0         | 0.0    |
| 5         | 0.0    |
| 15        | 3.0    |
| 35        | 3.0    |
| 45        | 2.0    |
| 55        | 0.0    |
| 65        | 0.0    |

**Discharge Limit (negative = discharge):**

| Temp (°C) | C-rate |
|-----------|--------|
| -25       | -0.2   |
| -15       | -0.2   |
| -10       | -1.0   |
| -5        | -1.5   |
| 0         | -2.0   |
| 5         | -4.5   |
| 10        | -5.0   |
| 25        | -5.0   |
| 30        | -4.5   |
| 35        | -4.0   |
| 45        | -3.8   |
| 55        | -3.8   |
| 60        | -0.2   |
| 65        | -0.2   |

**Notes:** Warning level ~58°C, Fault level ~63°C. Alternate current sensor option limits at ~3.5-3.8C.

### SoC-Based Current Limit (derating curve from integrator documentation)

**BOL Charge Limit:**

| SoC (%) | C-rate |
|---------|--------|
| 0       | 3.0    |
| 85      | 3.0    |
| 90      | 2.0    |
| 95      | 1.0    |
| 100     | 0.5    |

**BOL Discharge Limit:**

| SoC (%) | C-rate |
|---------|--------|
| 0       | -1.0   |
| 2       | -1.0   |
| 5       | -2.2   |
| 8       | -2.2   |
| 10      | -4.0   |
| 15      | -4.0   |
| 20      | -5.0   |
| 50      | -5.0   |
| 100     | -5.0   |

**Aged Charge Limit:**

| SoC (%) | C-rate |
|---------|--------|
| 0       | 3.0    |
| 85      | 3.0    |
| 90      | 1.5    |
| 95      | 0.8    |
| 100     | 0.0    |

**Aged Discharge Limit:**

| SoC (%) | C-rate |
|---------|--------|
| 0       | -1.0   |
| 2       | -1.5   |
| 5       | -2.2   |
| 10      | -2.8   |
| 15      | -3.0   |
| 20      | -3.1   |
| 25      | -3.5   |
| 30      | -3.8   |
| 40      | -4.0   |
| 50      | -4.5   |
| 100     | -4.5   |

### SEV-Based Current Limit (derating curve from integrator documentation)

**Charge Limit:**

| SEV (mV) | C-rate |
|----------|--------|
| 3000     | 3.0    |
| 4100     | 3.0    |
| 4200     | 0.0    |

**Discharge Limit:**

| SEV (mV) | C-rate |
|----------|--------|
| 3000     | 0.0    |
| 3200     | 0.0    |
| 3300     | -2.0   |
| 3400     | -2.5   |
| 3450     | -3.8   |
| 3550     | -5.0   |
| 4200     | -5.0   |

**Reference lines:** Fault level = 3000 mV, Warning level = 3200 mV.

**Confidence: MEDIUM-HIGH** — derived from integrator documentation. Some interpolation involved but breakpoints are clear.

---

## 5. Kirchhoff-Correct Bus Model for Parallel Packs

### Problem Statement
When N battery packs are connected in parallel on a shared DC bus, each pack has its own OCV (based on its SoC) and internal resistance. The bus voltage is a single shared value that must be solved.

### Correct Algorithm

For N packs in parallel with a total load current I_load:

1. Each pack's current: `I_k = (OCV_k - V_bus) / R_k`
2. KCL constraint: `Σ I_k = I_load`
3. Substituting: `Σ (OCV_k - V_bus) / R_k = I_load`
4. Solving for V_bus:

```python
def solve_bus_voltage(ocv_packs, r_packs, i_load):
    """
    Solve for DC bus voltage given parallel packs.
    
    ocv_packs: list of OCV values for each pack [V]
    r_packs: list of internal resistance for each pack [Ω]
    i_load: total load current [A] (positive = discharge)
    
    Returns: V_bus [V]
    """
    # Sum of (OCV_k / R_k)
    sum_ocv_over_r = sum(ocv / r for ocv, r in zip(ocv_packs, r_packs))
    # Sum of (1 / R_k)
    sum_inv_r = sum(1.0 / r for r in r_packs)
    
    # V_bus = (Σ(OCV_k/R_k) + I_load) / Σ(1/R_k)
    # Note: I_load > 0 for charging (current into packs).
    v_bus = (sum_ocv_over_r + i_load) / sum_inv_r
    
    return v_bus

# Then individual pack currents:
# I_k = (OCV_k - V_bus) / R_k
```

### Key Properties
- **Self-balancing**: A pack with higher SoC (higher OCV) naturally delivers more current
- **No iteration needed**: The linear model has a closed-form solution
- **Handles asymmetry**: Different SoC, temperature, and aging states per pack
- **Sign convention**: I_load > 0 for charging (into packs). I_k > 0 means pack is charging.

### When Non-Linear Effects Matter
The simple linear model assumes constant R and OCV during a timestep. For better accuracy:
- Update OCV and R at each simulation timestep based on new SoC and temperature
- For very fast transients, add RC circuit elements (Thévenin equivalent model)
- The Corvus BMS already handles current limiting per pack — the simulation should apply Figure 28/29/30 limits as clamps on I_k after solving

### Reference Approach
This is standard circuit theory (superposition / nodal analysis) applied to battery equivalents. It's used in:
- MATLAB/Simulink Simscape battery models (parallel branch resolution)
- PyBaMM (Python Battery Mathematical Modelling) for multi-cell parallel configurations
- Any power systems textbook covering parallel source networks

**Confidence: HIGH** — this is textbook electrical engineering. The closed-form solution is exact for the linear (Thévenin) battery model.

---

## Summary of Recommended Demo Parameters

| Parameter | Value | Source |
|-----------|-------|--------|
| R_module (25°C, mid-SoC) | 3.3 mΩ | Corvus manual p.32 |
| R_pack (22 modules series) | 72.6 mΩ | Derived |
| Cells in series per module | 14 | 50V / 3.6V |
| Cells in series per pack | 308 | 14 × 22 |
| Module capacity | 128 Ah | Corvus datasheet |
| Module weight | 60 kg | Corvus datasheet |
| Module energy | 5.6 kWh | Corvus datasheet |
| Pack voltage range | ~924–1291 V | OCV curve × 308 |
| Pack nominal voltage | ~1100 V | At ~50% SoC |
| Max C-rate (charge/discharge) | 3C / 5C | Figures 28-30 (BOL, optimal temp) |
| THERMAL_MASS | 1,268,000 J/°C | Composite: 70% cells × 1050 + 30% non-cell × 500 |
| THERMAL_COOLING_COEFF | 800 W/°C | Estimated for forced-air |
| NMC cell OCV range | 3.0 - 4.190 V | Literature (NMC 622) |
