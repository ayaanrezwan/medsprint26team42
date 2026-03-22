// ═══════════════════════════════════════════════════════════════════
//  KNEESAFE — Solenoid Lock Controller
//  Flash this file to the ESP32 on the brace.
//
//  Hardware wiring (as established):
//    GPIO 18  → Funduino motor driver IN1  (solenoid control)
//    GPIO  4  → Button LOCK   (other leg → GND)
//    GPIO  5  → Button UNLOCK (other leg → GND)
//    GPIO  2  → Built-in status LED (HIGH = locked)
//
//  Power:
//    Solenoid PSU (+) → driver +M / 9V terminal
//    Solenoid PSU (–) → driver GND  (also tied to ESP32 GND)
//    Driver 5V output → ESP32 VIN
//
//  No libraries required — standard Arduino ESP32 core only.
// ═══════════════════════════════════════════════════════════════════

#define SOLENOID_PIN  18   // HIGH = solenoid energized = knee LOCKED
#define BTN_LOCK       4   // press to lock   (INPUT_PULLUP: LOW = pressed)
#define BTN_UNLOCK     5   // press to unlock (INPUT_PULLUP: LOW = pressed)
#define STATUS_LED     2   // built-in LED

#define DEBOUNCE_MS   50   // ignore bounces shorter than this

bool locked = false;

// ─────────────────────────────────────────────────────────────────
void setSolenoid(bool engage) {
  locked = engage;
  digitalWrite(SOLENOID_PIN, engage ? HIGH : LOW);
  digitalWrite(STATUS_LED,   engage ? HIGH : LOW);
  Serial.println(engage ? "[BRACE] LOCKED" : "[BRACE] UNLOCKED");
}

// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] KneeSafe starting...");

  pinMode(SOLENOID_PIN, OUTPUT);
  pinMode(STATUS_LED,   OUTPUT);
  pinMode(BTN_LOCK,     INPUT_PULLUP);
  pinMode(BTN_UNLOCK,   INPUT_PULLUP);

  setSolenoid(false);   // start unlocked
  Serial.println("[BOOT] Ready. Press LOCK button to engage.\n");
}

// ─────────────────────────────────────────────────────────────────
void loop() {
  static unsigned long lastAction = 0;

  // Debounce gate — ignore any input within DEBOUNCE_MS of last action
  if (millis() - lastAction < DEBOUNCE_MS) return;

  bool lockPressed   = digitalRead(BTN_LOCK)   == LOW;
  bool unlockPressed = digitalRead(BTN_UNLOCK) == LOW;

  if (lockPressed && !locked) {
    setSolenoid(true);
    lastAction = millis();
  }

  if (unlockPressed && locked) {
    setSolenoid(false);
    lastAction = millis();
  }

  delay(10);
}
