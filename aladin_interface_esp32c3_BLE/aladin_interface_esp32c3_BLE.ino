/*
==========================================================================================

This project implements a very simple hardware interface allowing communication
between the Uwatec Aladin Pro and a computer via USB using an ESP32-C3.

The ESP32 acts as a transparent serial bridge between the host and the dive computer.

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

------------------------------------------------------------------------------------------

IMPORTANT:
GPIO1 is NOT used as a regular UART TX pin in this validated hardware.
It only drives the 2N2222 transistor stage.

==========================================================================================
*/

#include <Arduino.h>
#include <NimBLEDevice.h>

#define ALADIN_RX        5
#define ALADIN_TX        -1
#define PIN_TRIGGER      1

#define USB_BAUDRATE     115200
#define ALADIN_BAUDRATE  19200
#define INVERT_UART      false

#define LED_PIN          LED_BUILTIN
#define BLE_DEVICE_NAME  "Aladin-ESP32"

#define BLE_SERVICE_UUID "12345678-1234-1234-1234-1234567890AB"
#define BLE_CHAR_UUID    "12345678-1234-1234-1234-1234567890AC"

HardwareSerial AladinSerial(1);

volatile bool bleClientConnected = false;
NimBLECharacteristic* pNotifyChar = nullptr;

bool blePulseActive = false;
uint32_t blePulseUntilMs = 0;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    bleClientConnected = true;
    digitalWrite(LED_PIN, HIGH);
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    bleClientConnected = false;
    digitalWrite(LED_PIN, LOW);
    NimBLEDevice::startAdvertising();
  }
};

static void pulseBleLed() {
  blePulseActive = true;
  blePulseUntilMs = millis() + 25;
  digitalWrite(LED_PIN, LOW);
}

static void updateBleLed() {
  if (bleClientConnected) {
    if (blePulseActive) {
      if ((int32_t)(millis() - blePulseUntilMs) >= 0) {
        blePulseActive = false;
        digitalWrite(LED_PIN, HIGH);
      }
    } else {
      digitalWrite(LED_PIN, HIGH);
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}

void setup()
{
  pinMode(PIN_TRIGGER, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(PIN_TRIGGER, LOW);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(USB_BAUDRATE);

  AladinSerial.begin(ALADIN_BAUDRATE, SERIAL_8N1, ALADIN_RX, ALADIN_TX, INVERT_UART);

  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setDeviceName(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEServer* pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

  pNotifyChar = pService->createCharacteristic(
    BLE_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  pNotifyChar->setValue("ready");

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(BLE_DEVICE_NAME);
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->enableScanResponse(true);
  pAdvertising->start();
}

void loop()
{
  // PC/host -> Aladin
  while (Serial.available())
  {
    uint8_t stream = Serial.read();
    AladinSerial.write(stream);
  }

  // Aladin -> PC/host
  while (AladinSerial.available())
  {
    uint8_t stream = AladinSerial.read();
    Serial.write(stream);
  }

  // BLE test notification every second when connected
  static uint32_t lastNotifyMs = 0;
  uint32_t now = millis();

  if (bleClientConnected && (now - lastNotifyMs > 1000)) {
    lastNotifyMs = now;

    static uint32_t counter = 0;
    char message[20];
    snprintf(message, sizeof(message), "tick %lu", (unsigned long)counter++);

    pNotifyChar->setValue((uint8_t*)message, strlen(message));
    pNotifyChar->notify();

    pulseBleLed();
  }

  updateBleLed();
}