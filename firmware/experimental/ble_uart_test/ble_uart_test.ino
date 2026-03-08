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

BLE MIRRORING CONCEPT

HOST <-> USB <-> ESP32 <-> Aladin
                   |
                   +-> BLE monitor

IMPORTANT:
GPIO1 is NOT used as a regular UART TX pin in this validated hardware.
It only drives the 2N2222 transistor stage.
Using GPIO1 as direct UART TX breaks the interface behavior.

==========================================================================================
*/

#include <Arduino.h>
#include <NimBLEDevice.h>

// =========================
// Validated hardware config
// =========================
#define ALADIN_RX        5
#define ALADIN_TX        -1
#define PIN_TRIGGER      1

#define USB_BAUDRATE     115200
#define ALADIN_BAUDRATE  19200
#define INVERT_UART      false

#define LED_PIN          LED_BUILTIN

HardwareSerial AladinSerial(1);

// =========================
// BLE config
// =========================
#define BLE_DEVICE_NAME        "Aladin-ESP32-Monitor"
#define BLE_SERVICE_UUID       "12345678-1234-1234-1234-1234567890AB"
#define BLE_PC2ALADIN_UUID     "12345678-1234-1234-1234-1234567890AC"
#define BLE_ALADIN2PC_UUID     "12345678-1234-1234-1234-1234567890AD"

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pPcToAladinChar = nullptr;
NimBLECharacteristic* pAladinToPcChar = nullptr;

volatile bool bleClientConnected = false;

// BLE packet buffers
static uint8_t pcToAladinBuffer[20];
static size_t pcToAladinLength = 0;

static uint8_t aladinToPcBuffer[20];
static size_t aladinToPcLength = 0;

// Stats
volatile uint32_t pcToAladinCount = 0;
volatile uint32_t aladinToPcCount = 0;

// Activity blink
volatile bool bleActivityPulse = false;
uint32_t ledPulseUntilMs = 0;

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
  bleActivityPulse = true;
  ledPulseUntilMs = millis() + 25;
  digitalWrite(LED_PIN, LOW);   // brief blink
}

static void updateBleLed() {
  if (bleClientConnected) {
    if (bleActivityPulse) {
      if ((int32_t)(millis() - ledPulseUntilMs) >= 0) {
        bleActivityPulse = false;
        digitalWrite(LED_PIN, HIGH);
      }
    } else {
      digitalWrite(LED_PIN, HIGH);
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}

static void flushPcToAladinBLE() {
  if (!bleClientConnected || pcToAladinLength == 0) return;

  pPcToAladinChar->setValue(pcToAladinBuffer, pcToAladinLength);
  pPcToAladinChar->notify();
  pcToAladinLength = 0;
  pulseBleLed();
}

static void flushAladinToPcBLE() {
  if (!bleClientConnected || aladinToPcLength == 0) return;

  pAladinToPcChar->setValue(aladinToPcBuffer, aladinToPcLength);
  pAladinToPcChar->notify();
  aladinToPcLength = 0;
  pulseBleLed();
}

static void pushPcToAladinBLE(uint8_t stream) {
  pcToAladinBuffer[pcToAladinLength++] = stream;
  if (pcToAladinLength >= sizeof(pcToAladinBuffer)) {
    flushPcToAladinBLE();
  }
}

static void pushAladinToPcBLE(uint8_t stream) {
  aladinToPcBuffer[aladinToPcLength++] = stream;
  if (aladinToPcLength >= sizeof(aladinToPcBuffer)) {
    flushAladinToPcBLE();
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Validated transistor control state
  pinMode(PIN_TRIGGER, OUTPUT);
  digitalWrite(PIN_TRIGGER, LOW);

  Serial.begin(USB_BAUDRATE);
  delay(300);

  // RX-only UART toward Aladin, matching the validated working hardware
  AladinSerial.begin(ALADIN_BAUDRATE, SERIAL_8N1, ALADIN_RX, ALADIN_TX, INVERT_UART);

  // NimBLE
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setDeviceName(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

  pPcToAladinChar = pService->createCharacteristic(
    BLE_PC2ALADIN_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  pPcToAladinChar->setValue("pc2aladin");

  pAladinToPcChar = pService->createCharacteristic(
    BLE_ALADIN2PC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  pAladinToPcChar->setValue("aladin2pc");

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(BLE_DEVICE_NAME);
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->enableScanResponse(true);
  pAdvertising->start();
}

void loop() {
  // =========================
  // HOST -> Aladin
  // =========================
  while (Serial.available() > 0) {
    uint8_t stream = (uint8_t)Serial.read();

    // Keep the validated bridge behavior
    AladinSerial.write(stream);
    pcToAladinCount++;

    // BLE mirror
    pushPcToAladinBLE(stream);
  }

  // =========================
  // Aladin -> HOST
  // =========================
  while (AladinSerial.available() > 0) {
    uint8_t stream = (uint8_t)AladinSerial.read();

    Serial.write(stream);
    aladinToPcCount++;

    // BLE mirror
    pushAladinToPcBLE(stream);
  }

  // periodic BLE flush
  static uint32_t lastFlushMs = 0;
  uint32_t now = millis();
  if (now - lastFlushMs > 20) {
    flushPcToAladinBLE();
    flushAladinToPcBLE();
    lastFlushMs = now;
  }

  updateBleLed();
}