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
  
firmware/   -> ESP32 firmware  
hardware/   -> wiring and hardware documentation  
photos/     -> prototype pictures  
docs/       -> additional documentation  

---

#### ## Features
- Minimal hardware
- ESP32-C3 USB native interface
- Compatible with multiple dive log software
- No proprietary interface required

---

### # Hardware

### # Hardware Implementation
The circuit can be built on a small perfboard and fits under the ESP32-C3 Super Mini module.

A small terminal block can be used for the Aladin cable.

---

#### Required components:
- ESP32-C3 Super Mini
- 1 × NPN transistor (2N2222)
- 1 × 10kΩ resistor
- 1 × 2kΩ resistor
- 1 × 100kΩ resistor
- 2 wires to the Aladin contacts

---

#### Known working hardware

Validated working values:  

Resistors:  
- R1 = 10 kΩ (DATA sensing)  
- R2 = 2 kΩ  (base drive)  
- R3 = 100 kΩ (base pull-down)   
Transistor :  
- 2N2222  
Board :  
- ESP32-C3 Super Mini  


#### # Wiring

Working wiring:

ESP32-C3 Super Mini          2N2222               Uwatec Aladin Pro

GPIO5 --- R1 10kΩ -----------+-------------------- contact (-) DATA
                             |
                             C
GPIO1 --- R2 2kΩ ------------B
                             |
                             R3 100kΩ
                             |
GND -------------------------E-------------------- contact B GND---

---
#### ## Prototype
Prototype built on perfboard without ESP32-C3 super mini.

![Prototype 1](hardware/photos/20260307\_095238.jpg)
![Prototype 2](hardware/photos/20260307\_095252.jpg)

Other views in hardware/photos/  
---

#### # How it works

- The ESP32 acts as a **USB serial adapter**
- The transistor pulls the Aladin DATA line low when transmitting
- The DATA line is also monitored by the ESP32 through a resistor

---

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

### # Software
The ESP32 firmware is a minimal serial bridge.

Communication parameters:
PC baudrate: 115200
Aladin baudrate: 19200
Serial format: 8N1

---

#### # Firmware
The firmware can be compiled with **Arduino IDE** using the ESP32-C3 board package.

Main principle:
 	PC <-> USB Serial <-> ESP32 <-> UART <-> Aladin

---

## Tested software

The interface has been successfully tested with:

- **Wlog**
- **PCDive**
- **Subsurface**

Multiple connection cycles confirmed stable operation.

---

## Additional tested configuration

The interface was also successfully tested with **Subsurface on Android** using a **USB OTG connection**.

Validated setup:
- Android phone with USB OTG support
- ESP32-C3 Super Mini interface connected by USB
- Uwatec Aladin Pro connected to the interface
- Subsurface mobile on Android

Result:
- Dive computer detected
- Dive information readable
- Dive download working

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

> [!CAUTION]
> Use this interface at your own risk.

This project is not affiliated with or endorsed by Uwatec or Scubapro.

