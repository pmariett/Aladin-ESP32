# Aladin-ESP32

\# Uwatec Aladin Pro – ESP32-C3 Interface



Minimal hardware interface to download dive logs from an \*\*Uwatec Aladin Pro\*\* dive computer using an \*\*ESP32-C3 Super Mini\*\*.



The interface has been successfully tested with:



\- \*\*Wlog\*\*

\- \*\*PCDive\*\*

\- \*\*Subsurface\*\*



---



\# Overview



This project implements a very simple hardware interface allowing communication between the \*\*Uwatec Aladin Pro\*\* and a computer via USB using an \*\*ESP32-C3\*\*.



The ESP32 acts as a \*\*transparent serial bridge\*\* between the PC and the dive computer.



Only \*\*one transistor and three resistors\*\* are required.



---



\# Hardware



Required components:



\- ESP32-C3 Super Mini

\- 1 × NPN transistor (2N2222)

\- 1 × 10kΩ resistor

\- 1 × 2kΩ resistor

\- 1 × 100kΩ resistor

\- 2 wires to the Aladin contacts



---



\# Wiring

ESP32-C3                           NPN              ALADIN

super mini                        2N2222              Pro



GPIO5 ─── R10kΩ ───────────── C ───────── contact (-) (black wire)

GPIO1 ─── R2kΩ ────────────── B ─┐

&nbsp;                                     R100kΩ

GND ─────────────────────── E ─┴────── contact B   (red wire)





Where:



\- \*\*contact (-)\*\* = DATA

\- \*\*contact B\*\* = GND



---



\# How it works



\- The ESP32 acts as a \*\*USB serial adapter\*\*

\- The transistor pulls the Aladin DATA line low when transmitting

\- The DATA line is also monitored by the ESP32 through a resistor



The design was optimized experimentally for stability with the following resistor values:



\- 10kΩ (DATA sensing)

\- 2kΩ (base drive)

\- 100kΩ (base pull-down)



---



\# Software



The ESP32 firmware is a minimal serial bridge.



Communication parameters:

PC baudrate: 115200

Aladin baudrate: 19200

Serial format: 8N1




---



\# Tested Software



The interface has been successfully tested with:



\- \*\*Wlog\*\*

\- \*\*PCDive\*\*

\- \*\*Subsurface\*\*



Multiple connection cycles confirmed stable operation.



---



\# Firmware



The firmware can be compiled with \*\*Arduino IDE\*\* using the ESP32-C3 board package.



Main principle:

&nbsp;	PC <-> USB Serial <-> ESP32 <-> UART <-> Aladin



---



\# Hardware Implementation



The circuit can be built on a small perfboard and fits under the ESP32-C3 Super Mini module.



A small terminal block can be used for the Aladin cable.



---



\# License



MIT License



Copyright (c) 2026 Philippe Mariette



---



\# Acknowledgements



Project developed by \*\*Philippe Mariette\*\*.



Code refinement, debugging assistance and documentation were supported using \*\*ChatGPT (OpenAI)\*\*.



---



\# Disclaimer



Use this interface at your own risk.



This project is not affiliated with or endorsed by Uwatec or Scubapro.







