/*
==========================================================================================

This project implements a very simple hardware interface allowing communication 
 between the **Uwatec Aladin Pro** and a computer via USB using an **ESP32-C3**.

The ESP32 acts as a **transparent serial bridge** between the PC and the dive computer.


MIT License
Copyright (c) 2026 Philippe Mariette

------------------------------------------------------------------------------------------

WIRING

  ESP32-C3                   NPN            ALADIN
 super mini                 2N2222            Pro
 
 GPIO5 ─── R10kΩ ───────────── C ───────── contact (-) (black wire)
 GPIO1 ───  R2kΩ ───────────── B ─┐
                                R100kΩ
 GND ──────────────────────────E ─┴─────── contact B (red wire)

==========================================================================================
*/

#define ALADIN_RX 5               // ALADIN DATA (-) -> 10kΩ -> GPIO5
#define ALADIN_TX -1              // N/A
#define PIN_TRIGGER 1             // GPIO1 -> 2kΩ -> Base 2N2222
#define USB_BAUDRATE 115200       // vitesse échange port série du PC
#define ALADIN_BAUDRATE 19200     // vitesse échange port série Aladin
#define INVERT_UART false         // inversion des polarités RX/TX

HardwareSerial AladinSerial(1);   // Communication Série avec l'Aladin

void setup()
{
  pinMode(PIN_TRIGGER, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  digitalWrite(PIN_TRIGGER, LOW);

  Serial.begin(USB_BAUDRATE);

  AladinSerial.begin(ALADIN_BAUDRATE,SERIAL_8N1,ALADIN_RX,ALADIN_TX,INVERT_UART);
  
  digitalWrite(LED_BUILTIN, LOW);
}

void loop()
{
  // PC -> Aladin
  while (Serial.available())
  {
    digitalWrite(LED_BUILTIN, HIGH);
    uint8_t stream = Serial.read();
    AladinSerial.write(stream);
  }

  // Aladin -> PC
  while (AladinSerial.available())
  {
    digitalWrite(LED_BUILTIN, LOW);
    uint8_t stream = AladinSerial.read();
    Serial.write(stream);
  }
}