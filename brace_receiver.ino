// ═══════════════════════════════════════════════════════════════════
//  KNEESAFE — BRACE RECEIVER  ESP32 Firmware
//
//  This is the single, merged, ready-to-flash file for the brace ESP32.
//  It combines the servo controller (esp32.cpp) and the BLE server patch
//  (esp32_ble_patch.cpp) into one clean file, with the duplicate code
//  and concatenation errors removed.
//
//  Hardware on this board:
//    Servo signal wire     → GPIO 13  (PWM-capable)
//    Tongue pad LEFT       → GPIO 34  (analog, LOCK)
//    Tongue pad RIGHT      → GPIO 35  (analog, UNLOCK)
//    Tongue pad CENTRE     → GPIO 32  (analog, TOGGLE)
//    Status LED            → GPIO 2   (built-in, HIGH = locked)
//
//  Libraries needed — install all via Arduino IDE → Library Manager:
//    • ESP32Servo
//    • ArduinoJson
//    • WebSockets  (by Markus Sattler)
//    • BLE libraries are built into the ESP32 Arduino core — no install needed
//
//  Inputs (three ways to control the brace):
//    1. BLE write from retainer transmitter → "LOCK" / "UNLOCK" / "TOGGLE"
//    2. Tongue pressure pads (analog)       → LEFT / RIGHT / CENTRE hold
//    3. WebSocket command from dashboard    → "LOCK" / "UNLOCK"
//
//  All three paths call the same setBrace() function.
// ═══════════════════════════════════════════════════════════════════

#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── WiFi (for optional dashboard) ─────────────────────────────────
// If WiFi is unavailable the sketch will time out gracefully and
// continue — BLE and tongue pads will still work normally.
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ── Pin assignments ────────────────────────────────────────────────
#define SERVO_PIN      13
#define TONGUE_PIN_L   34   // GPIO 34 is input-only — no pull-up needed for analog
#define TONGUE_PIN_R   35   // GPIO 35 is input-only — same
#define TONGUE_PIN_C   32
#define STATUS_LED      2

// ── Servo angles ───────────────────────────────────────────────────
// Adjust after physical testing with your brace.
// 0°  = locked (servo extended / stiff)
// 90° = unlocked (free range of motion)
#define SERVO_LOCKED    0
#define SERVO_UNLOCKED  90

// ── Tongue pad tuning ──────────────────────────────────────────────
// ADC range: 0–4095. Print raw values first with Serial.println(),
// then set PRESS_THRESHOLD just above the resting (no-press) value.
#define PRESS_THRESHOLD  2000   // above this = "pressed"
#define HOLD_MS           300   // ms the pad must be held to confirm

// ── BLE configuration ──────────────────────────────────────────────
// These UUIDs must match retainer_transmitter.ino exactly.
#define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_DEVICE_NAME         "KneeSafe_Brace"

// ── State ──────────────────────────────────────────────────────────
Servo        braceServo;
bool         braceLocked = false;
bool         wasPressed  = false;
bool         actionTaken = false;
unsigned long holdStart  = 0;
bool         wifiOk      = false;

WebSocketsServer ws = WebSocketsServer(81);

// ── Forward declarations ───────────────────────────────────────────
void setBrace(bool lock, const char* reason);
void setServo(bool lock);
void sendStatus(int L, int R, int C);
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);

// ─────────────────────────────────────────────────────────────────
//  BLE SERVER CALLBACKS
//  The characteristic callback fires automatically on its own RTOS
//  task whenever the retainer writes a command — no polling needed
//  in loop().
// ─────────────────────────────────────────────────────────────────
class BLEServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        Serial.println("[BLE] Retainer connected.");
    }
    void onDisconnect(BLEServer* server) override {
        Serial.println("[BLE] Retainer disconnected — restarting advertising.");
        BLEDevice::startAdvertising();
    }
};

class BLECharCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        if (value.empty()) return;

        Serial.print("[BLE] Command from retainer: ");
        Serial.println(value.c_str());

        if      (value == "LOCK")   setBrace(true,  "RETAINER");
        else if (value == "UNLOCK") setBrace(false, "RETAINER");
        else if (value == "TOGGLE") setBrace(!braceLocked, "RETAINER");
        else    Serial.println("[BLE] Unknown command — ignored.");
    }
};

// ─────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n[BOOT] Brace receiver starting...");

    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);

    // ── Servo ────────────────────────────────────────────────────
    braceServo.attach(SERVO_PIN);
    setServo(false);
    Serial.println("[SERVO] Ready — starting UNLOCKED.");

    // ── WiFi (10-second timeout — non-blocking if unavailable) ───
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WIFI] Connecting");
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        wifiOk = true;
        Serial.print("\n[WIFI] Connected. Dashboard: http://");
        Serial.println(WiFi.localIP());
        ws.begin();
        ws.onEvent(onWebSocketEvent);
    } else {
        Serial.println("\n[WIFI] Not available — dashboard disabled.");
        Serial.println("       BLE and tongue pads still fully active.");
    }

    // ── BLE server ───────────────────────────────────────────────
    BLEDevice::init(BLE_DEVICE_NAME);

    BLEServer* bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new BLEServerCB());

    BLEService* bleService = bleServer->createService(BLE_SERVICE_UUID);

    BLECharacteristic* bleChar = bleService->createCharacteristic(
        BLE_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR  // write-without-response: lower latency
    );
    bleChar->setCallbacks(new BLECharCB());
    bleChar->addDescriptor(new BLE2902());

    bleService->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    BLEDevice::startAdvertising();

    Serial.println("[BLE] Advertising as KneeSafe_Brace.");
    Serial.println("[BOOT] Ready.\n");
}

// ─────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────
void loop() {
    // Keep WebSocket alive if WiFi is available
    if (wifiOk) ws.loop();

    // ── Read tongue pads ──────────────────────────────────────────
    int L = analogRead(TONGUE_PIN_L);
    int R = analogRead(TONGUE_PIN_R);
    int C = analogRead(TONGUE_PIN_C);

    bool leftPressed   = L > PRESS_THRESHOLD;
    bool rightPressed  = R > PRESS_THRESHOLD;
    bool centerPressed = C > PRESS_THRESHOLD;
    bool anyPressed    = leftPressed || rightPressed || centerPressed;

    // ── Debounce + hold detection ─────────────────────────────────
    if (anyPressed && !wasPressed) {
        holdStart   = millis();
        wasPressed  = true;
        actionTaken = false;
    }
    if (!anyPressed) {
        wasPressed  = false;
        actionTaken = false;
    }

    bool confirmed = wasPressed && !actionTaken && (millis() - holdStart > HOLD_MS);

    if (confirmed) {
        actionTaken = true;
        if      (leftPressed)   setBrace(true,          "TONGUE_LEFT");
        else if (rightPressed)  setBrace(false,         "TONGUE_RIGHT");
        else if (centerPressed) setBrace(!braceLocked,  "TONGUE_CENTRE");
    }

    // ── Send status to dashboard every 50ms ──────────────────────
    static unsigned long lastSend = 0;
    if (wifiOk && millis() - lastSend > 50) {
        sendStatus(L, R, C);
        lastSend = millis();
    }

    delay(10);
}

// ─────────────────────────────────────────────────────────────────
//  BRACE CONTROL
//  Single function called by all three input paths (BLE, tongue, dashboard).
// ─────────────────────────────────────────────────────────────────
void setBrace(bool lock, const char* reason) {
    braceLocked = lock;
    setServo(lock);
    digitalWrite(STATUS_LED, lock ? HIGH : LOW);
    Serial.printf("[BRACE] %s  [source: %s]\n",
                  lock ? "LOCKED" : "UNLOCKED", reason);
}

void setServo(bool lock) {
    braceServo.write(lock ? SERVO_LOCKED : SERVO_UNLOCKED);
}

// ─────────────────────────────────────────────────────────────────
//  WEBSOCKET — send JSON status to dashboard
// ─────────────────────────────────────────────────────────────────
void sendStatus(int L, int R, int C) {
    StaticJsonDocument<128> doc;
    doc["locked"] = braceLocked;
    doc["t1"]     = L;
    doc["t2"]     = R;
    doc["t3"]     = C;
    String json;
    serializeJson(doc, json);
    ws.broadcastTXT(json);
}

// ─────────────────────────────────────────────────────────────────
//  WEBSOCKET — receive commands from dashboard
// ─────────────────────────────────────────────────────────────────
void onWebSocketEvent(uint8_t num, WStype_t type,
                      uint8_t* payload, size_t length) {
    if (type == WStype_TEXT) {
        String msg = String((char*)payload);
        if      (msg == "LOCK")   setBrace(true,  "DASHBOARD");
        else if (msg == "UNLOCK") setBrace(false, "DASHBOARD");
    }
}
