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
#define BLE_DEVICE_NAME  "Aladin-ESP32-Monitor"

#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // phone -> ESP32
#define BLE_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // ESP32 -> phone

HardwareSerial AladinSerial(1);

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pTxChar = nullptr;
NimBLECharacteristic* pRxChar = nullptr;

volatile bool bleClientConnected = false;

// LED management
bool ledPulseActive = false;
uint32_t ledPulseUntilMs = 0;

// Trigger pulse mode
bool autoPulseEnabled = false;
uint32_t lastAutoPulseMs = 0;
const uint32_t autoPulsePeriodMs = 100;

// Stats
uint32_t rxByteCount = 0;
uint32_t bleWriteCount = 0;
uint32_t frameCount = 0;

// RX formatter
static const size_t RAW_BUF_SIZE = 8;
uint8_t rawBuffer[RAW_BUF_SIZE];
size_t rawLength = 0;
uint32_t lastRxByteMs = 0;
const uint32_t RAW_FLUSH_TIMEOUT_MS = 30;

// Duplicate filtering
uint8_t lastSentBuffer[RAW_BUF_SIZE];
size_t lastSentLength = 0;

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

static void bleSendText(const char* message) {
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
  char status[64];
  snprintf(
    status,
    sizeof(status),
    "STAT r=%lu b=%lu a=%c f=%lu",
    (unsigned long)rxByteCount,
    (unsigned long)bleWriteCount,
    autoPulseEnabled ? '1' : '0',
    (unsigned long)frameCount
  );
  bleSendText(status);
}

static bool isAllZero(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (data[i] != 0x00) return false;
  }
  return true;
}

static bool isDuplicateOfLast(const uint8_t* data, size_t len) {
  if (len != lastSentLength) return false;
  if (len == 0) return false;

  for (size_t i = 0; i < len; i++) {
    if (data[i] != lastSentBuffer[i]) return false;
  }
  return true;
}

static void rememberLast(const uint8_t* data, size_t len) {
  lastSentLength = len;
  for (size_t i = 0; i < len; i++) {
    lastSentBuffer[i] = data[i];
  }
}

static void flushRawBufferAsHex() {
  if (rawLength == 0) return;

  if (isAllZero(rawBuffer, rawLength)) {
    rawLength = 0;
    return;
  }

  if (isDuplicateOfLast(rawBuffer, rawLength)) {
    rawLength = 0;
    return;
  }

  rememberLast(rawBuffer, rawLength);
  frameCount++;

  char line[128];
  size_t pos = 0;

  pos += snprintf(line + pos, sizeof(line) - pos, "RX[%03lu] ", (unsigned long)frameCount);

  for (size_t i = 0; i < rawLength && pos < sizeof(line) - 4; i++) {
    pos += snprintf(line + pos, sizeof(line) - pos, "%02X", rawBuffer[i]);
    if (i + 1 < rawLength && pos < sizeof(line) - 2) {
      line[pos++] = ' ';
      line[pos] = '\0';
    }
  }

  bleSendText(line);
  rawLength = 0;
}

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string value = pCharacteristic->getValue();
    if (value.empty()) return;

    bleWriteCount++;

    String cmd = String(value.c_str());
    cmd.trim();

    if (cmd == "pulse") {
      triggerPulse();
      bleSendText("CMD pulse ok");
      return;
    }

    if (cmd == "auto on") {
      autoPulseEnabled = true;
      bleSendText("CMD auto on");
      return;
    }

    if (cmd == "auto off") {
      autoPulseEnabled = false;
      bleSendText("CMD auto off");
      return;
    }

    if (cmd == "status") {
      sendStatus();
      return;
    }

    char line[96];
    size_t pos = 0;
    pos += snprintf(line + pos, sizeof(line) - pos, "BLE ");

    for (size_t i = 0; i < value.length() && pos < sizeof(line) - 4; i++) {
      pos += snprintf(line + pos, sizeof(line) - pos, "%02X", (uint8_t)value[i]);
      if (i + 1 < value.length() && pos < sizeof(line) - 2) {
        line[pos++] = ' ';
        line[pos] = '\0';
      }
    }

    bleSendText(line);
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
  while (AladinSerial.available()) {
    uint8_t stream = AladinSerial.read();
    rxByteCount++;

    if (rawLength < RAW_BUF_SIZE) {
      rawBuffer[rawLength++] = stream;
    } else {
      flushRawBufferAsHex();
      rawBuffer[rawLength++] = stream;
    }

    lastRxByteMs = millis();
  }

  uint32_t now = millis();

  if (rawLength > 0 && (now - lastRxByteMs > RAW_FLUSH_TIMEOUT_MS)) {
    flushRawBufferAsHex();
  }

  if (autoPulseEnabled && (now - lastAutoPulseMs > autoPulsePeriodMs)) {
    lastAutoPulseMs = now;
    triggerPulse();
  }

  updateLed();
}