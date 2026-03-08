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


------------------------------------------------------------------------------------------

BLE MIRRORING CONNECT

PC <-> USB <-> ESP32 <-> Aladin
                 |
                 +-> BLE monitor

==========================================================================================
*/

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// =========================
// UART / USB config
// =========================
#define ALADIN_RX        5        // ALADIN DATA (-) -> 10kΩ -> GPIO5
#define ALADIN_TX        -1       // Non use
//#define ALADIN_TX      1        // GPIO1 is NOT used as a regular UART TX pin in this validated hardware. Using GPIO1 as direct UART TX breaks the interface behavior
#define ALADIN_BAUDRATE  19200    // vitesse échange port série Aladin
#define INVERT_UART      false    // inversion des polarités RX/TX
#define PIN_TRIGGER      1        // GPIO1 -> 2kΩ -> Base 2N2222  Only to drive the 2N2222 transistor stage.
#define USB_BAUDRATE     115200   // vitesse échange port série via USB

HardwareSerial AladinSerial(1);   // Communication Série avec l'Aladin

// =========================
// BLE config
// =========================
#define BLE_DEVICE_NAME        "Aladin-ESP32-Monitor"
#define BLE_SERVICE_UUID       "12345678-1234-1234-1234-1234567890AB"
#define BLE_PC2ALADIN_UUID     "12345678-1234-1234-1234-1234567890AC"
#define BLE_ALADIN2PC_UUID     "12345678-1234-1234-1234-1234567890AD"

BLEServer *pServer = nullptr;
BLECharacteristic *pPcToAladinChar = nullptr;
BLECharacteristic *pAladinToPcChar = nullptr;

volatile bool bleClientConnected = false;

// paquets BLE de 20 octets
static uint8_t pcToAladinBuf[20];
static size_t pcToAladinLen = 0;

static uint8_t aladinToPcBuf[20];
static size_t aladinToPcLen = 0;

// stats
volatile uint32_t pcToAladinCount = 0;
volatile uint32_t aladinToPcCount = 0;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleClientConnected = true;
    Serial.println("[BLE] Client connected");
  }

  void onDisconnect(BLEServer *server) override {
    bleClientConnected = false;
    Serial.println("[BLE] Client disconnected");
    BLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising restarted");
  }
};

static void flushPcToAladinBLE() {
  if (!bleClientConnected || pcToAladinLen == 0) return;
  pPcToAladinChar->setValue(pcToAladinBuf, pcToAladinLen);
  pPcToAladinChar->notify();
  pcToAladinLen = 0;
}

static void flushAladinToPcBLE() {
  if (!bleClientConnected || aladinToPcLen == 0) return;
  pAladinToPcChar->setValue(aladinToPcBuf, aladinToPcLen);
  pAladinToPcChar->notify();
  aladinToPcLen = 0;
}

static void pushPcToAladinBLE(uint8_t b) {
  pcToAladinBuf[pcToAladinLen++] = b;
  if (pcToAladinLen >= sizeof(pcToAladinBuf)) {
    flushPcToAladinBLE();
  }
}

static void pushAladinToPcBLE(uint8_t b) {
  aladinToPcBuf[aladinToPcLen++] = b;
  if (aladinToPcLen >= sizeof(aladinToPcBuf)) {
    flushAladinToPcBLE();
  }
}

static void blinkLed() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(10);
  digitalWrite(LED_BUILTIN, LOW);
}

void setup() {
  // initialisation LED et port USB
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(USB_BAUDRATE);
  delay(300);

  Serial.println();
  Serial.println("=== ALADIN USB + BLE MONITOR START ===");

  // état de repos validé
  pinMode(PIN_TRIGGER, OUTPUT);
  digitalWrite(PIN_TRIGGER, LOW);

  AladinSerial.begin(ALADIN_BAUDRATE, SERIAL_8N1, ALADIN_RX, ALADIN_TX, INVERT_UART);

  // BLE setup
  digitalWrite(LED_BUILTIN, LOW);        // allume la led interne en même temps qu'on active l'interface BLE
  BLEDevice::init(BLE_DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(BLE_SERVICE_UUID);

  pPcToAladinChar = pService->createCharacteristic(
    BLE_PC2ALADIN_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pPcToAladinChar->addDescriptor(new BLE2902());

  pAladinToPcChar = pService->createCharacteristic(
    BLE_ALADIN2PC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pAladinToPcChar->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();

  Serial.print("[BLE] Advertising as: ");
  Serial.println(BLE_DEVICE_NAME);
  Serial.println("[BLE] Use nRF Connect and enable notifications on both characteristics");
}

void loop() {
  // =========================
  // PC -> Aladin
  // =========================
  while (Serial.available() > 0) {
    uint8_t stream = (uint8_t)Serial.read();

    // vrai pont USB -> Aladin
    AladinSerial.write(stream);
    pcToAladinCount++;

    // miroir BLE
    pushPcToAladinBLE(stream);
  }

  // =========================
  // Aladin -> PC
  // =========================
  while (AladinSerial.available() > 0) {
    digitalWrite(LED_BUILTIN, HIGH);
    uint8_t stream = (uint8_t)AladinSerial.read();

    // vrai pont Aladin -> USB
    Serial.write(stream);
    aladinToPcCount++;

    // miroir BLE
    pushAladinToPcBLE(stream);
    digitalWrite(LED_BUILTIN, LOW);
  }

  // flush régulier BLE
  static uint32_t lastFlushMs = 0;
  uint32_t now = millis();
  if (now - lastFlushMs > 20) {
    flushPcToAladinBLE();
    flushAladinToPcBLE();
    lastFlushMs = now;
  }

  // stats périodiques
  static uint32_t lastStatsMs = 0;
  if (now - lastStatsMs > 3000) {
    Serial.println();
    Serial.print("[STATS] PC->ALADIN=");
    Serial.print(pcToAladinCount);
    Serial.print("  ALADIN->PC=");
    Serial.println(aladinToPcCount);
    lastStatsMs = now;
  }
}
