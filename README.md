<p align="center">
  <img src="docs/images/banner.svg">
</p>
# Aladin-ESP32 interface
![License](https://img.shields.io/github/license/pmariett/Aladin-ESP32)
![Platform](https://img.shields.io/badge/platform-ESP32--C3-blue)
![Hardware](https://img.shields.io/badge/hardware-2N2222%20minimal-green)
![Tested](https://img.shields.io/badge/tested-Wlog%20%7C%20PCDive%20%7C%20Subsurface-success)
![GitHub repo size](https://img.shields.io/github/repo-size/pmariett/Aladin-ESP32)
![GitHub stars](https://img.shields.io/github/stars/pmariett/Aladin-ESP32?style=social)

### # Uwatec Aladin Pro – ESP32-C3 Interface

Minimal hardware interface to download dive logs from an **Uwatec Aladin Pro** dive computer using an **ESP32-C3 Super Mini**.

The interface has been successfully tested with:
- **Wlog**
- **PCDive**
- **Subsurface**

---

### # Overview
This project implements a very simple hardware interface allowing communication between the **Uwatec Aladin Pro** and a computer via USB using an **ESP32-C3**.

The ESP32 acts as a **transparent serial bridge** between the PC and the dive computer.

Only **one transistor and three resistors** are required.

---

#### ## Repository structure

- firmware\/   -> ESP32 firmware
- hardware\/   -> wiring and hardware documentation
- photos\/     -> prototype pictures
- docs\/       -> additional documentation

---

#### ## Features
- Minimal hardware
- ESP32-C3 USB native interface
- Compatible with multiple dive log software
- No proprietary interface required

---

### # Hardware

#### Required components:
- ESP32-C3 Super Mini
- 1 × NPN transistor (2N2222)
- 1 × 10kΩ resistor
- 1 × 2kΩ resistor
- 1 × 100kΩ resistor
- 2 wires to the Aladin contacts

---

#### # Wiring
ESP32-C3                           NPN              ALADIN
super mini                        2N2222              Pro

GPIO5 ─── R10kΩ ───────────── C ───────── contact (-) (black wire)
GPIO1 ─── R2kΩ ────────────── B ─┐
                                      R100kΩ
GND ─────────────────────── E ─┴────── contact B   (red wire)

Where:
- **contact (-)** = DATA
- **contact B** = GND

#### ## Prototype
Prototype built on perfboard without ESP32-C3 super mini.

![Prototype 1](hardware/photos/20260307\_095238.jpg)
![Prototype 2](hardware/photos/20260307\_095252.jpg)

---

#### # How it works

- The ESP32 acts as a **USB serial adapter**
- The transistor pulls the Aladin DATA line low when transmitting
- The DATA line is also monitored by the ESP32 through a resistor

The design was optimized experimentally for stability with the following resistor values:

- 10kΩ (DATA sensing)
- 2kΩ (base drive)
- 100kΩ (base pull-down)

## Communication flow
PC software
&nbsp;  │
USB serial
&nbsp;  │
ESP32-C3
&nbsp;  │
UART bridge
&nbsp;  │
Uwatec Aladin Pro

---
#### ## Known working configuration

ESP32-C3 Super Mini  
Transistor: 2N2222  

Resistors:
- 10kΩ (DATA sensing)
- 2kΩ (base drive)
- 100kΩ (base pull-down)

Tested software:
- Wlog
- PCDive
- Subsurface

---

### # Software
The ESP32 firmware is a minimal serial bridge.

Communication parameters:
PC baudrate: 115200
Aladin baudrate: 19200
Serial format: 8N1

---

#### # Tested Software
The interface has been successfully tested with:

- **Wlog**
- **PCDive**
- **Subsurface**

Multiple connection cycles confirmed stable operation.

---

#### # Firmware
The firmware can be compiled with **Arduino IDE** using the ESP32-C3 board package.

Main principle:
 	PC <-> USB Serial <-> ESP32 <-> UART <-> Aladin

---

### # Hardware Implementation
The circuit can be built on a small perfboard and fits under the ESP32-C3 Super Mini module.

A small terminal block can be used for the Aladin cable.

---

### # License
MIT License
Copyright (c) 2026 Philippe Mariette

---

### # Acknowledgements
Project developed by **Philippe MARIETTE**.

Code refinement, debugging assistance and documentation were supported using **ChatGPT (OpenAI)**.

---

# Disclaimer

Use this interface at your own risk.

This project is not affiliated with or endorsed by Uwatec or Scubapro.

