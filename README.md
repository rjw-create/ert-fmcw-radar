# ERT+FMCW Radar — Handheld Subsurface Imaging Toolkit

> A portable, low-cost geophysical sensor that fuses **Electrical Resistivity Tomography (ERT)** and **24 GHz FMCW Radar** to map subsurface anomalies for paleontology, archaeology, and field geology.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: Arduino](https://img.shields.io/badge/Platform-Arduino%20Uno%20R3-00979D?logo=arduino)](https://www.arduino.cc/)
[![ADC: ADS1115](https://img.shields.io/badge/ADC-ADS1115%2016--bit-green)](https://www.ti.com/product/ADS1115)
[![Radar: LD2410C](https://img.shields.io/badge/Radar-HLK--LD2410C%2024GHz-orange)](https://hlktech.net/index.php?id=988)

---

## The Problem

Paleontologists and archaeologists still rely on manual excavation techniques that are slow, imprecise, and risk damaging buried artifacts. Professional ground-penetrating radar (GPR) systems cost **$15,000–$50,000+** and require trained operators. There is no affordable, handheld tool that combines multiple sensing modalities to help field researchers locate subsurface objects before digging.

## The Solution

This project delivers a **handheld, battery-powered device** with interchangeable sensor bases that performs two complementary types of subsurface scanning:

| Modality | How It Works | What It Detects |
|---|---|---|
| **ERT** (Electrical Resistivity Tomography) | Injects current through outer probes, measures voltage across inner probes via a 4-electrode Wenner array | Soil composition, moisture boundaries, resistivity anomalies from buried objects |
| **FMCW Radar** (24 GHz LD2410C) | Emits continuous radar waves into the ground, analyzes reflected energy and distance | Subsurface density changes, voids, metallic/mineral inclusions |

By **cross-referencing both datasets**, the system achieves higher confidence anomaly detection than either modality alone — at a total hardware cost under **$60**.

---

## System Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    HANDHELD UNIT                         │
│  ┌─────────┐    ┌───────────┐    ┌────────────────────┐  │
│  │ Battery │───▶│ Arduino  │◀─▶│ ADS1115 (16-bit)   │  │
│  │ 6700mAh │    │ Uno R3    │    │ 4-ch ADC via I²C   │  │
│  └─────────┘    │           │    └────────┬───────────┘  │
│                 │           │             │              │
│                 │   GPIO ◀──┼─────────────┘             │
│                 │   3,4     │                            │
│                 │ (SoftSer) │    ┌────────────────────┐  │
│                 │           │◀─▶│ HLK-LD2410C        │  │
│                 └─────┬─────┘    │ 24 GHz FMCW Radar  │  │
│                       │          └────────────────────┘  │
│                       │ USB Serial (115200 baud)         │
└───────────────────────┼──────────────────────────────────┘
                        ▼
              ┌──────────────────┐
              │  Host Computer   │
              │  Serial Monitor  │
              │  or MATLAB/Python│
              │  Data Logger     │
              └──────────────────┘
```

**Swappable Sensor Bases** — The device uses a dovetail slide mechanism allowing the user to swap between the ERT probe rail and the FMCW radar sled without rewiring.

---

## Repository Structure

```
├── firmware/
│   └── ert_fmcw_sensor.ino        # Arduino firmware — ADC + radar acquisition
├── analysis/
│   ├── visualize_scan.py           # Python script — load .mat data and plot
│   └── sample_data.mat             # Sample 180-scan capture from field test
├── hardware/
│   ├── schematic.md                # Circuit description and wiring guide
│   └── bom.md                     # Bill of materials with part numbers
├── docs/
│   └── images/                     # CAD renders, photos, diagrams
└── index.html                      # Portfolio page (GitHub Pages)
```

---

## Firmware Overview

The Arduino firmware (`firmware/ert_fmcw_sensor.ino`) runs a **250 ms acquisition loop** that:

1. **Polls the LD2410C** radar module over SoftwareSerial, parsing its proprietary binary frame protocol (header → length → payload → tail) to extract movement distance, movement energy, stationary distance, stationary energy, and detection distance.

2. **Reads 4 single-ended + 1 differential channel** from the ADS1115 at 16-bit resolution (±4.096 V range, GAIN_ONE). Each reading is a 2-sample average to reduce noise.

3. **Applies signal conditioning**:
   - Baseline calibration over the first 20 radar frames to establish ambient energy levels
   - Exponential moving average (α = 0.25) on both radar distance and score
   - Stale-data timeout (1200 ms) to zero out readings when the radar goes silent

4. **Streams CSV-formatted data** over USB serial at 115200 baud:
   ```
   ADC,<scan>,<a0>,<a1>,<a2>,<a3>,<d01>
   RADAR,<scan>,<status>,<detectDist>,<score>,<ageMs>
   ```

### Key Design Decisions

- **ADS1115 over Arduino's built-in ADC**: The onboard ADC is 10-bit (1024 levels). The ADS1115 provides 16-bit resolution (65,536 levels), essential for detecting the millivolt-scale potential differences in ERT measurements.
- **SoftwareSerial for radar**: Keeps the hardware UART free for PC data logging, avoiding the need for a multiplexer.
- **Exponential smoothing**: Balances responsiveness with noise rejection — important when the device is being physically moved across terrain.

---

## Analysis Pipeline

The Python analysis script converts the MATLAB `.mat` capture into visualizations:

```bash
cd analysis/
pip install scipy numpy matplotlib
python visualize_scan.py
```

This produces:
- **ERT voltage heatmap** — 5-channel ADC readings across 180 scan positions
- **Radar energy profile** — smoothed detection score vs. scan index
- **Radar distance map** — estimated reflector depth over the scan line
- **Fused anomaly overlay** — highlights regions where both modalities indicate subsurface features

---

## Hardware

### Bill of Materials

| Component | Part Number | Purpose | ~Cost |
|---|---|---|---|
| Arduino Uno R3 WiFi | A00066 | Microcontroller + serial interface | $25 |
| ADS1115 16-bit ADC | Adafruit ADS1115 | High-resolution ERT voltage measurement | $15 |
| HLK-LD2410C | LD2410C | 24 GHz FMCW radar module | $5 |
| Stainless steel probes (×4) | B0DSP9YH H7 | Ground-contact electrodes for ERT | $8 |
| Resistors (1kΩ, 2.2kΩ, 4.7kΩ) | B0F4P352 BB | Voltage divider + I²C pull-ups | $2 |
| 6700 mAh 5V battery pack | Onn. Portable | Field power supply | $12 |
| **Total** | | | **~$67** |

### Wiring Summary

- **ADS1115** → Arduino I²C (SDA/SCL) with 4.7kΩ pull-up
- **ERT probes** → ADS1115 channels A0–A3 through voltage divider (2.2kΩ / 1kΩ)
- **LD2410C** → Arduino pins 3 (RX) and 4 (TX) via SoftwareSerial
- **Power** → 5V USB from battery pack

See [`hardware/schematic.md`](hardware/schematic.md) for the full circuit description.

---

## Mechanical Design

The enclosure is 3D-printed in 7 interlocking sections (the design exceeded single-printer build volume). Key mechanical features:

- **Dovetail slide** — interchangeable base system for swapping between ERT and FMCW modules
- **FMCW sled** — hollow center channel provides unobstructed radar path to ground
- **ERT rail** — female dovetail block with 4 probe insertion holes
- **Velcro retention** — replaced thumb screws after iteration; easier field access

CAD was done in Inventor. Engineering drawings are in `docs/images/`.

---

## Results & Iteration

The project went through **5 prototype iterations**, evolving from a conceptual dovetail system to a fully integrated handheld device. Key milestones:

1. **Prototype 1** — Validated dovetail slide mechanism and radar module insertion concept
2. **Prototype 2** — Added ERT rail with probe holes; resolved base-swapping geometry
3. **Prototype 3** — Integrated electrical wiring through dovetail gap
4. **Final Electrical** — Full sensor fusion firmware with baseline calibration
5. **Final Mechanical** — 7-section print; velcro retention; field-tested

---

## Future Work

- Replace LD2410C with a true FMCW transceiver for raw I/Q data and depth profiling
- Add WiFi/Bluetooth streaming via ESP32 for tablet-based field visualization
- Implement 2D tomographic inversion on the ERT data using `pyGIMLi`
- Design a multi-electrode switching array for higher-resolution resistivity imaging

---

## Built With

- **Arduino IDE** — Firmware development
- **MATLAB** — Initial data capture and serial acquisition
- **Python / SciPy / Matplotlib** — Open-source analysis pipeline
- **Inventor** — Mechanical CAD and engineering drawings

---

## Context

This project was developed for the **TSA Engineering Design** competition (2025–2026) under the challenge theme *"Engineering the Tools of Scientific Discovery"*, responding to the National Academy of Engineering's Grand Challenge to build better tools for scientific exploration.

---

## License

This project is released under the [MIT License](LICENSE).
