/*
==========================================================================================

Uwatec Aladin Pro experimental BLE interface
for the validated ESP32-C3 + 2N2222 hardware.

IMPORTANT:
With the validated hardware, GPIO1 is NOT a real UART TX.
It only drives the 2N2222 transistor stage.

So this sketch is NOT a full bidirectional serial bridge.
It is an experimental BLE autonomous monitor with optional trigger pulses.

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

#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // phone -> ESP32
#define BLE_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // ESP32 -> phone

HardwareSerial AladinSerial(1);

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pTxChar = nullptr;
NimBLECharacteristic* pRxChar = nullptr;

volatile bool bleClientConnected = false;

static uint8_t txBuffer[20];
static size_t txLength = 0;

bool ledPulseActive = false;
uint32_t ledPulseUntilMs = 0;

bool autoPulseEnabled = false;
uint32_t lastAutoPulseMs = 0;
const uint32_t autoPulsePeriodMs = 1000;

uint32_t rxByteCount = 0;
uint32_t bleWriteCount = 0;

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

static void pulseLed() {
  ledPulseActive = true;
  ledPulseUntilMs = millis() + 20;
  digitalWrite(LED_PIN, LOW);
}

static void updateLed() {
  if (bleClientConnected) {
    if (ledPulseActive) {
      if ((int32_t)(millis() - ledPulseUntilMs) >= 0) {
        ledPulseActive = false;
        digitalWrite(LED_PIN, HIGH);
      }
    } else {
      digitalWrite(LED_PIN, HIGH);
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}

static void txFlush() {
  if (!bleClientConnected || txLength == 0) return;

  pTxChar->setValue(txBuffer, txLength);
  pTxChar->notify();
  txLength = 0;
  pulseLed();
}

static void txPush(uint8_t stream) {
  txBuffer[txLength++] = stream;
  if (txLength >= sizeof(txBuffer)) {
    txFlush();
  }
}

static void txText(const char* message) {
  if (!bleClientConnected) return;

  pTxChar->setValue((const uint8_t*)message, strlen(message));
  pTxChar->notify();
  pulseLed();
}

static void triggerPulse(uint32_t pulseMs = 25) {
  digitalWrite(PIN_TRIGGER, HIGH);
  delay(pulseMs);
  digitalWrite(PIN_TRIGGER, LOW);
}

static void sendStatus() {
  char status[96];
  snprintf(
    status,
    sizeof(status),
    "rx=%lu ble=%lu auto=%s",
    (unsigned long)rxByteCount,
    (unsigned long)bleWriteCount,
    autoPulseEnabled ? "on" : "off"
  );
  txText(status);
}

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    String value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    bleWriteCount++;

    // Commandes texte simples depuis nRF Connect
    // "pulse"  -> impulsion sur PIN_TRIGGER
    // "auto on" / "auto off"
    // "status"
    if (value == "pulse") {
      triggerPulse();
      txText("pulse ok");
      return;
    }

    if (value == "auto on") {
      autoPulseEnabled = true;
      txText("auto on");
      return;
    }

    if (value == "auto off") {
      autoPulseEnabled = false;
      txText("auto off");
      return;
    }

    if (value == "status") {
      sendStatus();
      return;
    }

    // Sinon, on renvoie les octets reçus côté BLE pour debug
    txText("ble rx");
    for (size_t i = 0; i < value.length(); i++) {
      txPush((uint8_t)value[i]);
    }
    txFlush();
  }
};

void setup() {
  pinMode(PIN_TRIGGER, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(PIN_TRIGGER, LOW);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(USB_BAUDRATE);

  AladinSerial.begin(ALADIN_BAUDRATE, SERIAL_8N1, ALADIN_RX, ALADIN_TX, INVERT_UART);

  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setDeviceName(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

  pTxChar = pService->createCharacteristic(
    BLE_TX_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  pTxChar->setValue("ready");

  pRxChar = pService->createCharacteristic(
    BLE_RX_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  pRxChar->setCallbacks(new RxCallbacks());

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(BLE_DEVICE_NAME);
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->enableScanResponse(true);
  pAdvertising->start();
}

void loop() {
  // Aladin -> BLE
  while (AladinSerial.available()) {
    uint8_t stream = AladinSerial.read();
    rxByteCount++;
    txPush(stream);
  }

  static uint32_t lastFlushMs = 0;
  uint32_t now = millis();

  if (now - lastFlushMs > 20) {
    txFlush();
    lastFlushMs = now;
  }

  if (autoPulseEnabled && (now - lastAutoPulseMs > autoPulsePeriodMs)) {
    lastAutoPulseMs = now;
    triggerPulse();
  }

  updateLed();
}