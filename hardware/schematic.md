# Circuit Schematic & Wiring Guide

## Block Diagram

```
                    ┌────────────────────────┐
                    │     Arduino Uno R3      │
                    │                         │
    USB ◀───────────┤ Serial (115200)         │
                    │                         │
                    │ A4 (SDA) ──┬── ADS1115 SDA (pin 6)
                    │ A5 (SCL) ──┤── ADS1115 SCL (pin 5)
                    │            │
                    │            └── 4.7kΩ pull-ups to VDD
                    │                         │
                    │ D3 (RX) ───── LD2410C TX │
                    │ D4 (TX) ───── LD2410C RX │
                    │                         │
                    │ 5V ────────── VDD rail   │
                    │ GND ───────── GND rail   │
                    └────────────────────────┘
```

## ADS1115 Connections

| ADS1115 Pin | Connected To | Notes |
|---|---|---|
| VDD | Arduino 5V | Power supply |
| GND | Arduino GND | Ground |
| SCL | Arduino A5 | I²C clock, 4.7kΩ pull-up to 5V |
| SDA | Arduino A4 | I²C data, 4.7kΩ pull-up to 5V |
| A0 | Probe A | Current injection electrode |
| A1 | Probe M | Inner voltage sense (+) |
| A2 | Probe N | Inner voltage sense (−) |
| A3 | Probe B | Current return electrode |
| ADDR | GND | I²C address = 0x48 (default) |

## ERT Electrode Array (Wenner Configuration)

```
    5V ──── R3 (1kΩ) ──── Probe A ─────── Probe M ─────── Probe N ─────── Probe B ──── GND
                            │                │                │                │
                          ADS A0           ADS A1           ADS A2           ADS A3
```

- **Outer probes (A, B)**: Current injection / return path through the soil
- **Inner probes (M, N)**: Measure the resulting voltage potential difference
- **R3 (1kΩ)**: Current-limiting resistor on the injection path
- **R2 (2.2kΩ)**: Voltage divider component for level scaling

## LD2410C Radar Module

| LD2410C Pin | Connected To | Notes |
|---|---|---|
| VCC | Arduino 5V | 5V tolerant |
| GND | Arduino GND | Ground |
| TX | Arduino D3 | SoftwareSerial RX |
| RX | Arduino D4 | SoftwareSerial TX |
| OUT | (unused) | Digital presence output |

## Power

- **6700 mAh USB battery pack** provides 5V via USB-A to the Arduino
- Arduino regulator supplies 5V rail to ADS1115 and LD2410C
- Estimated runtime: ~12+ hours continuous scanning
