// ═══════════════════════════════════════════════════════════════════
//  KNEESAFE — RETAINER TRANSMITTER  ESP32 Firmware
//
//  Hardware on this board:
//    Tactile button LEFT   → GPIO 25 and GND
//    Tactile button RIGHT  → GPIO 26 and GND
//    Tactile button CENTRE → GPIO 32 and GND
//    Status LED            → GPIO 2  (HIGH = connected to brace)
//
//  No external resistors needed — using internal pull-up resistors.
//  Resting state: pin reads HIGH.
//  Button pressed: pin reads LOW.
//
//  WHY NOT GPIO 34/35:
//    GPIO 34 and 35 on the ESP32 are input-only pins and do not
//    have internal pull-up resistors. Tactile buttons need a pull-up
//    to sit at a known resting state. Using GPIO 25, 26, and 32
//    avoids needing any external resistors at all.
//    If your physical layout requires 34/35, add a 10kΩ resistor
//    from each of those pins to 3.3V.
//
//  Commands sent to brace over BLE:
//    LEFT held HOLD_MS   → "LOCK"
//    RIGHT held HOLD_MS  → "UNLOCK"
//    CENTRE held HOLD_MS → "TOGGLE"
//
//  These strings are handled by setBrace() in brace_receiver.ino.
// ═══════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ── Button pins ───────────────────────────────────────────────────
#define BTN_LEFT    25
#define BTN_RIGHT   26
#define BTN_CENTRE  32
#define STATUS_LED   2

// ── Hold time ─────────────────────────────────────────────────────
// The button must stay pressed for this long before the command fires.
// 300ms prevents accidental triggers from jaw movement or swallowing
// while still feeling immediate to the patient.
// If false triggers occur during testing, raise this to 500.
#define HOLD_MS  300

// ── BLE configuration ─────────────────────────────────────────────
// These must match the values in brace_receiver.ino exactly.
#define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BRACE_DEVICE_NAME       "KneeSafe_Brace"

// ── State ─────────────────────────────────────────────────────────
static bool          bleConnected  = false;
static bool          deviceFound   = false;
static unsigned long holdStart     = 0;
static bool          btnWasPressed = false;
static bool          actionDone    = false;
static char          pendingCmd[8] = "";

static BLEClient*               bleClient   = nullptr;
static BLERemoteCharacteristic* bleChar     = nullptr;
static BLEAdvertisedDevice*     braceDevice = nullptr;

// ─────────────────────────────────────────────────────────────────
//  BLE SCAN CALLBACK
//  Stops scanning the moment the brace is found by name.
// ─────────────────────────────────────────────────────────────────
class ScanCB : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice device) override {
        if (device.getName() == BRACE_DEVICE_NAME) {
            Serial.println("[SCAN] Brace found.");
            BLEDevice::getScan()->stop();
            braceDevice = new BLEAdvertisedDevice(device);
            deviceFound = true;
        }
    }
};

// ─────────────────────────────────────────────────────────────────
//  BLE CLIENT CALLBACKS
// ─────────────────────────────────────────────────────────────────
class ClientCB : public BLEClientCallbacks {
    void onConnect(BLEClient* client) override {
        bleConnected = true;
        digitalWrite(STATUS_LED, HIGH);
        Serial.println("[BLE] Connected to brace.");
    }

    void onDisconnect(BLEClient* client) override {
        bleConnected = false;
        bleChar      = nullptr;
        digitalWrite(STATUS_LED, LOW);
        Serial.println("[BLE] Disconnected. Will reconnect.");
    }
};

// ─────────────────────────────────────────────────────────────────
//  connectToBrace()
// ─────────────────────────────────────────────────────────────────
static bool connectToBrace() {
    if (bleClient == nullptr) {
        bleClient = BLEDevice::createClient();
        bleClient->setClientCallbacks(new ClientCB());
    }
    if (!bleClient->connect(braceDevice)) {
        Serial.println("[BLE] Connection failed.");
        return false;
    }
    BLERemoteService* service = bleClient->getService(BLE_SERVICE_UUID);
    if (!service) {
        Serial.println("[BLE] Service UUID not found on brace.");
        bleClient->disconnect();
        return false;
    }
    bleChar = service->getCharacteristic(BLE_CHARACTERISTIC_UUID);
    if (!bleChar) {
        Serial.println("[BLE] Characteristic UUID not found on brace.");
        bleClient->disconnect();
        return false;
    }
    Serial.println("[BLE] Ready to send commands.");
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  sendCommand()
// ─────────────────────────────────────────────────────────────────
static void sendCommand(const char* cmd) {
    if (!bleConnected || bleChar == nullptr) {
        Serial.println("[CMD] Not connected — attempting reconnect.");
        if (!connectToBrace()) {
            Serial.println("[CMD] Reconnect failed. Command dropped.");
            return;
        }
    }
    bleChar->writeValue((uint8_t*)cmd, strlen(cmd));
    Serial.print("[CMD] Sent: ");
    Serial.println(cmd);
}

// ─────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n[BOOT] Retainer transmitter starting...");

    // INPUT_PULLUP: pin sits HIGH at rest, goes LOW when button pressed
    pinMode(BTN_LEFT,   INPUT_PULLUP);
    pinMode(BTN_RIGHT,  INPUT_PULLUP);
    pinMode(BTN_CENTRE, INPUT_PULLUP);

    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);

    BLEDevice::init("KneeSafe_Retainer");

    Serial.println("[SCAN] Scanning for brace...");
    BLEScan* scanner = BLEDevice::getScan();
    scanner->setAdvertisedDeviceCallbacks(new ScanCB());
    scanner->setActiveScan(true);
    scanner->start(10, false);

    unsigned long start = millis();
    while (!deviceFound && millis() - start < 12000) delay(100);

    if (deviceFound) connectToBrace();
    else Serial.println("[SCAN] Brace not found on startup. Will retry in loop.");

    Serial.println("[BOOT] Ready.\n");
}

// ─────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────
void loop() {

    // ── Reconnection handling ─────────────────────────────────────
    if (!deviceFound) {
        Serial.println("[SCAN] Scanning for brace...");
        BLEScan* scanner = BLEDevice::getScan();
        scanner->setAdvertisedDeviceCallbacks(new ScanCB());
        scanner->start(5, false);
        delay(6000);
        if (deviceFound) connectToBrace();
        return;
    }
    if (!bleConnected) {
        Serial.println("[BLE] Attempting reconnect...");
        connectToBrace();
        delay(2000);
        return;
    }

    // ── Read buttons ──────────────────────────────────────────────
    // INPUT_PULLUP: LOW = pressed, HIGH = not pressed
    bool leftPressed   = digitalRead(BTN_LEFT)   == LOW;
    bool rightPressed  = digitalRead(BTN_RIGHT)  == LOW;
    bool centrePressed = digitalRead(BTN_CENTRE) == LOW;
    bool anyPressed    = leftPressed || rightPressed || centrePressed;

    // ── Hold detection ────────────────────────────────────────────
    // Start timer on first press, fire command once hold is confirmed,
    // reset everything when all buttons are released.
    if (anyPressed && !btnWasPressed) {
        holdStart     = millis();
        btnWasPressed = true;
        actionDone    = false;

        if      (leftPressed)   strncpy(pendingCmd, "LOCK",   sizeof(pendingCmd));
        else if (rightPressed)  strncpy(pendingCmd, "UNLOCK", sizeof(pendingCmd));
        else if (centrePressed) strncpy(pendingCmd, "TOGGLE", sizeof(pendingCmd));
    }

    if (!anyPressed) {
        btnWasPressed = false;
        actionDone    = false;
        pendingCmd[0] = '\0';
    }

    if (btnWasPressed && !actionDone && millis() - holdStart > HOLD_MS) {
        actionDone = true;
        Serial.printf("[BTN] Hold confirmed → %s\n", pendingCmd);
        sendCommand(pendingCmd);
    }

    delay(10);
}
