#!/usr/bin/env python3
"""
plot_firmware.py — Plot Corvus Orca ESS firmware demo output

Reads CSV from stdin or file argument, generates a 5-panel dark-theme plot.
Saves to firmware_plot.png in the parent directory.
"""

import sys
import csv
import os
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

# ── Read CSV ─────────────────────────────────────────────────────────
if len(sys.argv) > 1:
    f = open(sys.argv[1], 'r')
else:
    f = sys.stdin

reader = csv.DictReader(f)
rows = list(reader)
if f is not sys.stdin:
    f.close()

if not rows:
    print("No data!", file=sys.stderr)
    sys.exit(1)

t = np.array([float(r['time_s']) for r in rows])
soc = np.array([float(r['soc_pct']) for r in rows])
cell_mv = np.array([float(r['cell_mv']) for r in rows])
temp_dc = np.array([float(r['temperature_deci_c']) for r in rows])
current = np.array([float(r['current_ma']) for r in rows])
chg_lim = np.array([float(r['charge_limit_ma']) for r in rows])
dchg_lim = np.array([float(r['discharge_limit_ma']) for r in rows])
mode = np.array([int(r['mode']) for r in rows])
contactor = np.array([int(r['contactor_state']) for r in rows])
warnings = np.array([int(r['warnings']) for r in rows])
faults = np.array([int(r['faults']) for r in rows])

# Convert units
cell_v = cell_mv / 1000.0
temp_c = temp_dc / 10.0
current_a = current / 1000.0
chg_lim_a = chg_lim / 1000.0
dchg_lim_a = dchg_lim / 1000.0

# ── Thresholds from bms_config.h ─────────────────────────────────────
OV_WARN_V = 4.210
OV_FAULT_V = 4.225
HW_OV_V = 4.300
OT_WARN_C = 60.0
OT_FAULT_C = 65.0
HW_OT_C = 70.0

# ── Mode names ───────────────────────────────────────────────────────
MODE_NAMES = {0: 'OFF', 1: 'POWER_SAVE', 2: 'FAULT', 3: 'READY',
              4: 'CONNECTING', 5: 'CONNECTED', 6: 'NOT_READY'}

# ── Dark theme ───────────────────────────────────────────────────────
plt.style.use('dark_background')
fig, axes = plt.subplots(5, 1, figsize=(14, 16), sharex=True,
                          gridspec_kw={'hspace': 0.25})

fig.suptitle('Corvus Orca ESS Firmware Demo — BQ76952 + STM32F446 + FreeRTOS',
             fontsize=14, fontweight='bold', color='white', y=0.98)

colors = {'main': '#4FC3F7', 'charge': '#66BB6A', 'discharge': '#FF7043',
          'warn': '#FFD54F', 'fault': '#EF5350', 'hw': '#AB47BC',
          'limit': '#78909C'}

# ── Panel 1: SoC ────────────────────────────────────────────────────
ax = axes[0]
ax.plot(t, soc, color=colors['main'], linewidth=1.5, label='SoC')
ax.set_ylabel('SoC (%)')
ax.set_title('State of Charge', fontsize=11)
ax.set_ylim(0, 105)
ax.legend(loc='upper left', fontsize=8)
ax.grid(alpha=0.2)

# Annotate phase transitions
phase_events = []
for i in range(1, len(mode)):
    if mode[i] != mode[i-1]:
        phase_events.append((t[i], MODE_NAMES.get(mode[i], '?'),
                             MODE_NAMES.get(mode[i-1], '?')))

for te, to_mode, from_mode in phase_events:
    ax.axvline(te, color='white', alpha=0.3, linestyle='--', linewidth=0.5)

# ── Panel 2: Cell Voltage ────────────────────────────────────────────
ax = axes[1]
ax.plot(t, cell_v, color=colors['main'], linewidth=1.5, label='Cell Voltage')
ax.axhline(OV_WARN_V, color=colors['warn'], linestyle='--', alpha=0.7,
           label=f'OV Warn ({OV_WARN_V}V)')
ax.axhline(OV_FAULT_V, color=colors['fault'], linestyle='--', alpha=0.7,
           label=f'OV Fault ({OV_FAULT_V}V)')
ax.axhline(HW_OV_V, color=colors['hw'], linestyle=':', alpha=0.5,
           label=f'HW Safety ({HW_OV_V}V)')
ax.set_ylabel('Cell Voltage (V)')
ax.set_title('Series Element Voltage — Table 13', fontsize=11)
ax.legend(loc='upper left', fontsize=8)
ax.grid(alpha=0.2)

# ── Panel 3: Temperature ────────────────────────────────────────────
ax = axes[2]
ax.plot(t, temp_c, color=colors['main'], linewidth=1.5, label='Temperature')
ax.axhline(OT_WARN_C, color=colors['warn'], linestyle='--', alpha=0.7,
           label=f'OT Warn ({OT_WARN_C}°C)')
ax.axhline(OT_FAULT_C, color=colors['fault'], linestyle='--', alpha=0.7,
           label=f'OT Fault ({OT_FAULT_C}°C)')
ax.axhline(HW_OT_C, color=colors['hw'], linestyle=':', alpha=0.5,
           label=f'HW Safety ({HW_OT_C}°C)')
ax.set_ylabel('Temperature (°C)')
ax.set_title('Cell Temperature — Table 13', fontsize=11)
ax.legend(loc='upper left', fontsize=8)
ax.grid(alpha=0.2)

# ── Panel 4: Current & Limits ───────────────────────────────────────
ax = axes[3]
ax.plot(t, current_a, color=colors['main'], linewidth=1.5, label='Pack Current')
ax.plot(t, chg_lim_a, color=colors['charge'], linewidth=1, linestyle='--',
        alpha=0.7, label='Charge Limit')
ax.plot(t, -dchg_lim_a, color=colors['discharge'], linewidth=1, linestyle='--',
        alpha=0.7, label='Discharge Limit')
ax.set_ylabel('Current (A)')
ax.set_title('Pack Current & Limits — §7.4', fontsize=11)
ax.legend(loc='upper left', fontsize=8)
ax.grid(alpha=0.2)

# ── Panel 5: Mode & Contactor ───────────────────────────────────────
ax = axes[4]

# Map mode enum to y-axis positions
mode_y = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6}
mode_labels = ['OFF', 'POWER_SAVE', 'FAULT', 'READY', 'CONNECTING', 'CONNECTED', 'NOT_READY']

mode_mapped = np.array([mode_y.get(m, 0) for m in mode])
contactor_mapped = contactor.astype(float) * (6.0 / 5.0)  # scale to same y range

ax.step(t, mode_mapped, color=colors['main'], linewidth=2, where='post',
        label='Pack Mode')
ax.step(t, contactor_mapped, color=colors['charge'], linewidth=1.5,
        where='post', alpha=0.6, label='Contactor State')

ax.set_ylabel('Mode')
ax.set_yticks(range(7))
ax.set_yticklabels(mode_labels, fontsize=8)
ax.set_xlabel('Time (s)')
ax.set_title('Pack Operation Mode — §7.1', fontsize=11)
ax.legend(loc='upper right', fontsize=8)
ax.grid(alpha=0.2)

# ── Warning/Fault shading ───────────────────────────────────────────
for ax_i in axes:
    # Shade warning regions
    warn_start = None
    for i in range(len(warnings)):
        if warnings[i] and warn_start is None:
            warn_start = t[i]
        elif not warnings[i] and warn_start is not None:
            ax_i.axvspan(warn_start, t[i], color=colors['warn'], alpha=0.05)
            warn_start = None
    if warn_start is not None:
        ax_i.axvspan(warn_start, t[-1], color=colors['warn'], alpha=0.05)

    # Shade fault regions
    fault_start = None
    for i in range(len(faults)):
        if faults[i] and fault_start is None:
            fault_start = t[i]
        elif not faults[i] and fault_start is not None:
            ax_i.axvspan(fault_start, t[i], color=colors['fault'], alpha=0.08)
            fault_start = None
    if fault_start is not None:
        ax_i.axvspan(fault_start, t[-1], color=colors['fault'], alpha=0.08)

# ── Save ─────────────────────────────────────────────────────────────
out_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        '..', 'firmware_plot.png')
fig.savefig(out_path, dpi=150, bbox_inches='tight', facecolor=fig.get_facecolor())
print(f"Saved: {out_path}", file=sys.stderr)
plt.close()
