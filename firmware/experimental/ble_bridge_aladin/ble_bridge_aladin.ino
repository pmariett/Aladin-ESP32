/*
==========================================================================================

Uwatec Aladin Pro experimental BLE interface
ESP32-C3 Super Mini + 2N2222
Raw triggered capture version

Hardware:
- GPIO5  = RX from Aladin
- GPIO1  = trigger pulse via transistor stage (NOT a normal UART TX)
- BLE    = Nordic UART Service (NUS)

Main behavior:
- watches UART stream for trigger pattern 55 55 55 00
- stores raw bytes continuously after trigger
- allows status / peek / dump over BLE NUS
- mirrors debug to USB Serial

MIT License
Copyright (c) 2026 Philippe Mariette

==========================================================================================
*/

#include <Arduino.h>
#include <NimBLEDevice.h>

// ---------------------------------------------------------------------------------------
// Pins / serial
// ---------------------------------------------------------------------------------------

#define ALADIN_RX        5
#define ALADIN_TX        -1
#define PIN_TRIGGER      1

#define USB_BAUDRATE     115200
#define ALADIN_BAUDRATE  19200
#define INVERT_UART      false

#define LED_PIN          LED_BUILTIN

// ---------------------------------------------------------------------------------------
// BLE NUS
// ---------------------------------------------------------------------------------------

#define BLE_DEVICE_NAME  "Aladin-ESP32-Monitor"

#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// ---------------------------------------------------------------------------------------
// Trigger / capture
// ---------------------------------------------------------------------------------------

static const uint8_t TRIG0 = 0x55;
static const uint8_t TRIG1 = 0x55;
static const uint8_t TRIG2 = 0x55;
static const uint8_t TRIG3 = 0x00;

// Raw byte capture size
#define MAX_CAPTURE_BYTES 1200

// Auto pulse
const uint32_t autoPulsePeriodMs = 1000;

// BLE pacing
const uint16_t bleLineDelayMs = 10;

// ---------------------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------------------

HardwareSerial AladinSerial(1);

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pTxChar = nullptr;
NimBLECharacteristic* pRxChar = nullptr;

volatile bool bleClientConnected = false;

// LED
bool ledPulseActive = false;
uint32_t ledPulseUntilMs = 0;

// Stats
uint32_t rxByteCount = 0;
uint32_t bleWriteCount = 0;
uint32_t captureStartRxIndex = 0;

// Pulse mode
bool autoPulseEnabled = false;
uint32_t lastAutoPulseMs = 0;

// Trigger detection
uint8_t triggerWindow[4] = {0, 0, 0, 0};
bool captureArmed = true;
bool captureTriggered = false;
bool captureDone = false;
bool captureDoneNotified = false;

// Capture storage
uint8_t captureBytes[MAX_CAPTURE_BYTES];
uint16_t captureLen = 0;

// ---------------------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------------------

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

static void usbLog(const char* msg) {
  Serial.println(msg);
}

static void bleSendText(const char* message) {
  // Always mirror to USB serial for debugging
  Serial.print("BLE TX: ");
  Serial.println(message);

  if (!bleClientConnected) return;

  pTxChar->setValue((const uint8_t*)message, strlen(message));
  pTxChar->notify();
  pulseLed();
  delay(bleLineDelayMs);
}

static void triggerPulse(uint32_t pulseMs = 25) {
  Serial.print("TRIGGER pulse ");
  Serial.print(pulseMs);
  Serial.println(" ms");

  digitalWrite(PIN_TRIGGER, HIGH);
  delay(pulseMs);
  digitalWrite(PIN_TRIGGER, LOW);
}

static void resetCapture() {
  captureArmed = true;
  captureTriggered = false;
  captureDone = false;
  captureDoneNotified = false;
  captureLen = 0;
  captureStartRxIndex = 0;

  triggerWindow[0] = 0;
  triggerWindow[1] = 0;
  triggerWindow[2] = 0;
  triggerWindow[3] = 0;
}

static void pushTriggerWindow(uint8_t b) {
  triggerWindow[0] = triggerWindow[1];
  triggerWindow[1] = triggerWindow[2];
  triggerWindow[2] = triggerWindow[3];
  triggerWindow[3] = b;
}

static bool triggerMatched() {
  return triggerWindow[0] == TRIG0 &&
         triggerWindow[1] == TRIG1 &&
         triggerWindow[2] == TRIG2 &&
         triggerWindow[3] == TRIG3;
}

static void markCaptureDoneIfNeeded() {
  if (captureDone && !captureDoneNotified) {
    captureDoneNotified = true;
    bleSendText("CAPTURE done");
  }
}

static void storeCapturedByte(uint8_t b) {
  if (captureDone) return;

  if (captureLen < MAX_CAPTURE_BYTES) {
    captureBytes[captureLen++] = b;
  } else {
    captureDone = true;
    markCaptureDoneIfNeeded();
  }
}

static void sendInfo() {
  char line[128];

  bleSendText("INFO device=ESP32-C3 Aladin BLE monitor");
  bleSendText("INFO mode=raw triggered capture");

  snprintf(line, sizeof(line), "INFO usb_baud=%u", USB_BAUDRATE);
  bleSendText(line);

  snprintf(line, sizeof(line), "INFO aladin_baud=%u", ALADIN_BAUDRATE);
  bleSendText(line);

  snprintf(line, sizeof(line), "INFO invert_uart=%u", INVERT_UART ? 1 : 0);
  bleSendText(line);

  snprintf(line, sizeof(line), "INFO trigger=%02X %02X %02X %02X",
           TRIG0, TRIG1, TRIG2, TRIG3);
  bleSendText(line);

  snprintf(line, sizeof(line), "INFO max_capture_bytes=%u", MAX_CAPTURE_BYTES);
  bleSendText(line);

  snprintf(line, sizeof(line), "INFO auto_pulse_period_ms=%lu",
           (unsigned long)autoPulsePeriodMs);
  bleSendText(line);
}

static void sendStatus() {
  char line[160];

  snprintf(
    line,
    sizeof(line),
    "STAT r=%lu b=%lu a=%c arm=%c trg=%c done=%c n=%u start=%lu",
    (unsigned long)rxByteCount,
    (unsigned long)bleWriteCount,
    autoPulseEnabled ? '1' : '0',
    captureArmed ? '1' : '0',
    captureTriggered ? '1' : '0',
    captureDone ? '1' : '0',
    captureLen,
    (unsigned long)captureStartRxIndex
  );

  bleSendText(line);
}

static void dumpRange(uint16_t maxBytesToSend) {
  if (captureLen == 0) {
    bleSendText("DUMP empty");
    return;
  }

  char line[128];
  uint16_t count = captureLen;

  if (maxBytesToSend > 0 && count > maxBytesToSend) {
    count = maxBytesToSend;
  }

  for (uint16_t i = 0; i < count; i += 8) {
    uint16_t remain = count - i;
    uint8_t n = (remain >= 8) ? 8 : remain;

    // Build line incrementally
    int pos = snprintf(line, sizeof(line), "%04u:", i);
    for (uint8_t k = 0; k < n && pos < (int)sizeof(line) - 4; k++) {
      pos += snprintf(line + pos, sizeof(line) - pos, " %02X", captureBytes[i + k]);
    }

    bleSendText(line);
  }

  if (count < captureLen) {
    snprintf(line, sizeof(line), "DUMP partial %u/%u bytes", count, captureLen);
    bleSendText(line);
  } else {
    bleSendText("DUMP end");
  }
}

static void dumpCapture() {
  dumpRange(0);
}

static void peekCapture() {
  dumpRange(64);
}

// ---------------------------------------------------------------------------------------
// Incoming UART processing
// ---------------------------------------------------------------------------------------

static void processIncomingByte(uint8_t b) {
  rxByteCount++;
  pushTriggerWindow(b);

  if (captureArmed && !captureTriggered && triggerMatched()) {
    captureArmed = false;
    captureTriggered = true;
    captureDone = false;
    captureDoneNotified = false;

    // Include the trigger bytes themselves in capture
    captureLen = 0;
    captureStartRxIndex = rxByteCount - 4;

    storeCapturedByte(TRIG0);
    storeCapturedByte(TRIG1);
    storeCapturedByte(TRIG2);
    storeCapturedByte(TRIG3);

    Serial.println("TRIGGER matched: 55 55 55 00");
    bleSendText("TRIGGER 55 55 55 00");
    return;
  }

  if (!captureTriggered || captureDone) {
    return;
  }

  storeCapturedByte(b);
}

// ---------------------------------------------------------------------------------------
// BLE callbacks
// ---------------------------------------------------------------------------------------

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    bleClientConnected = true;
    digitalWrite(LED_PIN, HIGH);
    Serial.println("BLE client connected");
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    bleClientConnected = false;
    digitalWrite(LED_PIN, LOW);
    Serial.print("BLE client disconnected, reason=");
    Serial.println(reason);
    NimBLEDevice::startAdvertising();
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string value = pCharacteristic->getValue();
    if (value.empty()) return;

    bleWriteCount++;

    Serial.print("BLE RX raw: ");
    Serial.println(value.c_str());

    String cmd = String(value.c_str());
    cmd.trim();

    Serial.print("BLE RX cmd: ");
    Serial.println(cmd);

    if (cmd == "status") {
      sendStatus();
      return;
    }

    if (cmd == "info") {
      sendInfo();
      return;
    }

    if (cmd == "dump") {
      dumpCapture();
      return;
    }

    if (cmd == "peek") {
      peekCapture();
      return;
    }

    if (cmd == "clear") {
      resetCapture();
      bleSendText("CMD clear ok");
      return;
    }

    if (cmd == "arm") {
      resetCapture();
      bleSendText("CMD arm ok");
      return;
    }

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

    bleSendText("CMD ?");
  }
};

// ---------------------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------------------

void setup() {
  pinMode(PIN_TRIGGER, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(PIN_TRIGGER, LOW);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(USB_BAUDRATE);
  delay(200);

  Serial.println();
  Serial.println("BOOT OK");
  Serial.println("Uwatec Aladin Pro BLE monitor - raw capture");

  AladinSerial.begin(ALADIN_BAUDRATE, SERIAL_8N1, ALADIN_RX, ALADIN_TX, INVERT_UART);

  resetCapture();

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

  Serial.println("BLE advertising started");
}

void loop() {
  while (AladinSerial.available()) {
    uint8_t b = (uint8_t)AladinSerial.read();
    processIncomingByte(b);
  }

  uint32_t now = millis();

  if (autoPulseEnabled && (now - lastAutoPulseMs > autoPulsePeriodMs)) {
    lastAutoPulseMs = now;
    triggerPulse();
  }

  markCaptureDoneIfNeeded();
  updateLed();
}