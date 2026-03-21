// ═══════════════════════════════════════════════════════════════════
//  BLE SERVER PATCH FOR esp32.cpp  (brace side)
//
//  The existing esp32.cpp already handles:
//    - Servo locking via setBrace()
//    - Tongue pad reading
//    - WiFi + WebSocket dashboard
//
//  This patch adds one thing:
//    A BLE server that accepts "LOCK", "UNLOCK", "TOGGLE" commands
//    from the retainer transmitter and calls the same setBrace()
//    function that everything else already uses.
//
//  HOW TO APPLY THIS PATCH:
//  ─────────────────────────
//  1. Open esp32.cpp
//
//  2. At the very top, after the existing #include lines, add:
//       #include <BLEDevice.h>
//       #include <BLEServer.h>
//       #include <BLEUtils.h>
//       #include <BLE2902.h>
//
//  3. After the existing #define lines (PRESS_THRESHOLD etc), add
//     the BLE UUID defines:
//       #define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
//       #define BLE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
//       #define BLE_DEVICE_NAME         "KneeSafe_Brace"
//
//  4. After the existing state variables (braceLocked, ws, etc),
//     paste in the two callback classes below (BLEServerCB and BLECharCB).
//     They call setBrace() which already exists in esp32.cpp.
//
//  5. At the END of setup(), after ws.onEvent(onWebSocketEvent),
//     paste in the BLE server setup block below.
//
//  That is the entire change. Nothing in loop() needs to change.
//  The BLE stack runs on its own RTOS task inside the ESP32 core —
//  the characteristic callback fires automatically when the retainer
//  sends a command, without any polling needed in loop().
// ═══════════════════════════════════════════════════════════════════


// ─────────────────────────────────────────────────────────────────
//  STEP 2 — ADD THESE INCLUDES at the top of esp32.cpp
//  Place them directly after the existing #include lines
// ─────────────────────────────────────────────────────────────────

/*
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
*/


// ─────────────────────────────────────────────────────────────────
//  STEP 3 — ADD THESE DEFINES after the existing #define block
// ─────────────────────────────────────────────────────────────────

/*
#define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_DEVICE_NAME         "KneeSafe_Brace"
*/


// ─────────────────────────────────────────────────────────────────
//  STEP 4 — PASTE THESE TWO CLASSES into esp32.cpp
//  Place them after the state variables, before setup()
//
//  These classes call setBrace() which already exists in esp32.cpp.
//  No changes needed to setBrace() itself.
// ─────────────────────────────────────────────────────────────────

class BLEServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        Serial.println("[BLE] Retainer connected.");
    }

    void onDisconnect(BLEServer* server) override {
        Serial.println("[BLE] Retainer disconnected. Restarting advertising.");
        // Restart advertising so the retainer can reconnect automatically
        BLEDevice::startAdvertising();
    }
};

class BLECharCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        if (value.empty()) return;

        Serial.print("[BLE] Command from retainer: ");
        Serial.println(value.c_str());

        // Call the same setBrace() that tongue pads and dashboard use.
        // "LOCK" and "UNLOCK" match the existing WebSocket commands exactly.
        if      (value == "LOCK")   setBrace(true,  "RETAINER");
        else if (value == "UNLOCK") setBrace(false, "RETAINER");
        else if (value == "TOGGLE") setBrace(!braceLocked, "RETAINER");
        else    Serial.println("[BLE] Unknown command — ignored.");
    }
};


// ─────────────────────────────────────────────────────────────────
//  STEP 5 — PASTE THIS BLOCK at the end of setup() in esp32.cpp
//  Place it after:  ws.onEvent(onWebSocketEvent);
// ─────────────────────────────────────────────────────────────────

/*
  // ── BLE server — accepts commands from retainer transmitter ──────
  // The ESP32 supports BLE + WiFi simultaneously (coexistence mode).
  // No extra configuration needed — this is the Arduino core default.
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
  // ── end BLE setup ─────────────────────────────────────────────
*/
