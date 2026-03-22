# Retain — MedSprint 2026

A hackathon prototype for muscular dystrophy patients. The system is a knee brace with an electromagnetic solenoid lock that holds the knee in an extended position to prevent collapse. It is controlled by two push buttons on the brace itself.

---

## Files

### `brace_receiver.ino` — **Flash this file to the ESP32**
The only firmware file that needs to be flashed. Controls the solenoid lock via the Funduino motor driver.

- **Language:** C++ (Arduino)
- **Board:** ESP32
- **Libraries:** None beyond the standard ESP32 Arduino core
- **Inputs:** Two push buttons (GPIO 4 = lock, GPIO 5 = unlock)
- **Output:** GPIO 18 → Funduino motor driver IN1 → solenoid

---

### `main.html` — Live monitoring dashboard
A browser-based dashboard that connects to the ESP32 over WebSocket and displays brace state, sensor readings, and an event log in real time. Open directly in a browser — no server needed.

- **Language:** HTML / CSS / JavaScript
- **Dependencies:** Chart.js (loaded from CDN)
- **Protocol:** WebSocket on port 81
- **Usage:** Enter the ESP32's IP address in the dashboard, click Connect

---

### `esp32.cpp` — Earlier firmware draft (not used)
A previous iteration of the brace firmware. It used a servo motor instead of a solenoid, tongue pressure pads instead of push buttons, WiFi + WebSocket for the dashboard, and a BNO085 IMU for fall detection. Superseded by `brace_receiver.ino`.

- **Language:** C++ (Arduino)
- **Libraries:** ESP32Servo, ArduinoJson, WebSockets

---

### `esp32_ble_patch.cpp` — BLE add-on patch (not used)
A patch file written to add Bluetooth Low Energy (BLE) support to `esp32.cpp`. It defines a BLE server on the brace that accepts `LOCK`, `UNLOCK`, and `TOGGLE` commands from a separate retainer transmitter device. Not applicable to the current hardware setup.

- **Language:** C++ (Arduino)
- **Libraries:** BLEDevice, BLEServer, BLEUtils, BLE2902 (all built into the ESP32 Arduino core)

---

### `retainer_transmitter.ino` — Second ESP32 firmware (not used)
Firmware for a second ESP32 that would be embedded in a dental retainer. It scans for and connects to the brace over BLE, then sends lock/unlock commands when buttons are pressed. Not used in the current single-button prototype.

- **Language:** C++ (Arduino)
- **Libraries:** BLEDevice, BLEClient, BLEScan (all built into the ESP32 Arduino core)

---

### `train.py` — LSTM fall prediction model (not used in prototype)
A Python script that trains a small LSTM neural network to predict fall events from IMU accelerometer and gyroscope data. Uses synthetic data for the hackathon; includes notes on using real datasets (SisFall). Produces a `fall_model.h5` file intended for deployment on a Raspberry Pi or via EloquentML on the ESP32.

- **Language:** Python 3
- **Libraries:** TensorFlow / Keras, NumPy, pandas, scikit-learn
- **Run:** `pip install tensorflow numpy pandas scikit-learn` then `python train.py`
- **Output:** `fall_model.h5`, `scaler.pkl`

---

### `RetainerBrace_Website/index.html` — Project landing page
A public-facing website describing the KneeSafe project for the hackathon presentation.

- **Language:** HTML / CSS / JavaScript

---

### `RetainerBrace_Deck.pptx` — Pitch deck
Slide deck for the MedSprint 2026 presentation.

### `RetainerBrace_MedSprint2026.docx` — Project document
Written project description and documentation.

### `RetainerBrace_FigmaJam_Presentation_Prompt.md` — FigmaJam prompt (presentation)
Prompt used to generate the FigmaJam presentation board.

### `RetainerBrace_FigmaJam_Prompt.md` — FigmaJam prompt (design)
Prompt used to generate the FigmaJam design board.

### `RetainerBrace_FigmaMake_Prompt.md` — Figma Make prompt
Prompt used to generate designs via Figma Make.

---

## Hardware (current prototype)

| Component | Role |
|---|---|
| ESP32 | Microcontroller |
| Funduino motor driver (L293D) | Drives the solenoid |
| Solenoid | Physical knee lock |
| Button (GPIO 4) | Engages lock |
| Button (GPIO 5) | Releases lock |
| 1N4007 flyback diode | Protects driver from solenoid voltage spikes |
| 100µF capacitor | Smooths power supply when solenoid fires |

**Power:** Solenoid runs on its own supply connected to the driver's `+M` terminal. The driver's 5V output powers the ESP32 — one power source total.
