#!/usr/bin/env python3
"""
visualize_scan.py
=================
Load a .mat scan capture from the ERT+FMCW subsurface imaging sensor
and produce diagnostic visualizations.

Usage:
    python visualize_scan.py                          # uses sample_data.mat
    python visualize_scan.py --file path/to/scan.mat  # custom file

Requirements:
    pip install scipy numpy matplotlib

Author:  [Your Name]
License: MIT
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.gridspec import GridSpec

try:
    import scipy.io as sio
except ImportError:
    sys.exit("ERROR: scipy is required.  Install with:  pip install scipy")


# ─── Data Loading ─────────────────────────────────────────────────────────────

def load_scan(filepath: str) -> dict:
    """
    Load a .mat file exported by the MATLAB serial acquisition script.

    Expected variables:
        adcData    (5 × N)  — rows: [A0, A1, A2, A3, D01] in volts
        radarDist  (1 × N)  — smoothed detection distance (cm)
        radarScore (1 × N)  — smoothed combined energy score
        radarAge   (1 × N)  — milliseconds since last valid radar frame
        maxScans   (scalar) — total number of scan positions
    """
    raw = sio.loadmat(filepath)

    data = {}
    for key in ["adcData", "radarDist", "radarScore", "radarAge", "maxScans"]:
        if key in raw:
            data[key] = np.squeeze(np.array(raw[key], dtype=np.float64))

    if "adcData" not in data:
        sys.exit(f"ERROR: '{filepath}' does not contain 'adcData'.")

    n_scans = data["adcData"].shape[1] if data["adcData"].ndim == 2 else len(data["adcData"])
    data["scan_axis"] = np.arange(1, n_scans + 1)

    return data


# ─── Visualization ────────────────────────────────────────────────────────────

CHANNEL_LABELS = ["A0 (Probe A)", "A1 (Probe M)", "A2 (Probe N)", "A3 (Probe B)", "D01 (M−N diff)"]


def plot_ert_heatmap(ax, data):
    """5-channel ERT voltage readings as a heatmap across scan positions."""
    adc = data["adcData"]
    im = ax.imshow(adc, aspect="auto", cmap="inferno",
                   extent=[1, adc.shape[1], 4.5, -0.5])
    ax.set_yticks(range(5))
    ax.set_yticklabels(CHANNEL_LABELS, fontsize=8)
    ax.set_xlabel("Scan Position")
    ax.set_title("ERT Voltage Heatmap", fontweight="bold")
    plt.colorbar(im, ax=ax, label="Volts", shrink=0.8)


def plot_ert_traces(ax, data):
    """Overlay line traces of all 5 ADC channels."""
    adc = data["adcData"]
    x = data["scan_axis"]
    for i, label in enumerate(CHANNEL_LABELS):
        ax.plot(x, adc[i], linewidth=0.8, label=label, alpha=0.85)
    ax.set_xlabel("Scan Position")
    ax.set_ylabel("Volts")
    ax.set_title("ERT Channel Traces", fontweight="bold")
    ax.legend(fontsize=7, ncol=3, loc="upper right")
    ax.grid(True, alpha=0.3)


def plot_radar_score(ax, data):
    """Radar combined energy score vs. scan position."""
    if "radarScore" not in data:
        ax.text(0.5, 0.5, "No radar score data", transform=ax.transAxes, ha="center")
        return
    x = data["scan_axis"]
    score = data["radarScore"][:len(x)]
    ax.fill_between(x, 0, score, alpha=0.4, color="#e74c3c")
    ax.plot(x, score, color="#c0392b", linewidth=1)
    ax.set_xlabel("Scan Position")
    ax.set_ylabel("Energy Score")
    ax.set_title("FMCW Radar — Reflected Energy", fontweight="bold")
    ax.grid(True, alpha=0.3)


def plot_radar_distance(ax, data):
    """Radar detection distance (estimated reflector depth) vs. scan."""
    if "radarDist" not in data:
        ax.text(0.5, 0.5, "No radar distance data", transform=ax.transAxes, ha="center")
        return
    x = data["scan_axis"]
    dist = data["radarDist"][:len(x)]
    ax.plot(x, dist, color="#2980b9", linewidth=1)
    ax.fill_between(x, 0, dist, alpha=0.2, color="#3498db")
    ax.set_xlabel("Scan Position")
    ax.set_ylabel("Distance (cm)")
    ax.set_title("FMCW Radar — Detection Distance", fontweight="bold")
    ax.invert_yaxis()  # Depth increases downward
    ax.grid(True, alpha=0.3)


def plot_fused_anomaly(ax, data):
    """
    Fused anomaly indicator: product of normalized ERT differential
    variance and radar score. High values = both modalities agree
    that something interesting is underground.
    """
    x = data["scan_axis"]

    # ERT anomaly: rolling standard deviation of the differential channel
    diff = data["adcData"][4]  # D01 channel
    window = min(7, len(diff) // 4)
    if window < 2:
        ert_anomaly = np.abs(diff - np.mean(diff))
    else:
        ert_anomaly = np.array([
            np.std(diff[max(0, i - window):i + window + 1])
            for i in range(len(diff))
        ])

    # Normalize both signals to [0, 1]
    def norm01(arr):
        mn, mx = arr.min(), arr.max()
        return (arr - mn) / (mx - mn + 1e-12)

    ert_n = norm01(ert_anomaly)

    if "radarScore" in data:
        radar_n = norm01(data["radarScore"][:len(x)])
        fused = ert_n * radar_n
        label = "ERT × Radar (fused)"
    else:
        fused = ert_n
        label = "ERT anomaly only"

    ax.fill_between(x, 0, fused, alpha=0.5, color="#27ae60")
    ax.plot(x, fused, color="#1e8449", linewidth=1.2)
    ax.set_xlabel("Scan Position")
    ax.set_ylabel("Anomaly Strength")
    ax.set_title("Fused Anomaly Indicator", fontweight="bold")
    ax.legend([label], fontsize=8)
    ax.grid(True, alpha=0.3)


def create_dashboard(data, output_path="scan_dashboard.png"):
    """Assemble the full 5-panel diagnostic dashboard."""
    fig = plt.figure(figsize=(14, 12), facecolor="white")
    fig.suptitle("ERT + FMCW Radar — Subsurface Scan Dashboard",
                 fontsize=16, fontweight="bold", y=0.98)

    gs = GridSpec(3, 2, figure=fig, hspace=0.38, wspace=0.30,
                  left=0.07, right=0.95, top=0.93, bottom=0.05)

    plot_ert_heatmap(fig.add_subplot(gs[0, :]), data)
    plot_ert_traces(fig.add_subplot(gs[1, 0]), data)
    plot_radar_score(fig.add_subplot(gs[1, 1]), data)
    plot_radar_distance(fig.add_subplot(gs[2, 0]), data)
    plot_fused_anomaly(fig.add_subplot(gs[2, 1]), data)

    fig.savefig(output_path, dpi=180, bbox_inches="tight")
    print(f"Dashboard saved → {output_path}")
    plt.show()


# ─── CLI Entry Point ──────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Visualize ERT+FMCW subsurface scan data from .mat files."
    )
    parser.add_argument(
        "--file", "-f",
        default="sample_data.mat",
        help="Path to .mat scan file (default: sample_data.mat)"
    )
    parser.add_argument(
        "--output", "-o",
        default="scan_dashboard.png",
        help="Output image path (default: scan_dashboard.png)"
    )
    args = parser.parse_args()

    if not Path(args.file).exists():
        sys.exit(f"ERROR: File not found: {args.file}")

    data = load_scan(args.file)
    n = data["adcData"].shape[1] if data["adcData"].ndim == 2 else len(data["adcData"])
    print(f"Loaded {n} scans from '{args.file}'")
    print(f"  ADC channels: {data['adcData'].shape[0]}")
    if "radarScore" in data:
        print(f"  Radar score range: {data['radarScore'].min():.2f} – {data['radarScore'].max():.2f}")
    if "radarDist" in data:
        print(f"  Radar dist range:  {data['radarDist'].min():.1f} – {data['radarDist'].max():.1f} cm")

    create_dashboard(data, args.output)


if __name__ == "__main__":
    main()
