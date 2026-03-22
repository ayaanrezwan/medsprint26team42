// Knee lock solenoid controller
// ESP32 + Funduino motor driver (L293D)
// GPIO 12 = lock button, GPIO 14 = unlock button, GPIO 18 = motor driver IN1

#define PIN_LOCK_BTN    12
#define PIN_UNLOCK_BTN  14
#define PIN_SOLENOID    18

bool isLocked = false;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LOCK_BTN,   INPUT_PULLUP);
  pinMode(PIN_UNLOCK_BTN, INPUT_PULLUP);
  pinMode(PIN_SOLENOID,   OUTPUT);

  // Start unlocked (solenoid de-energized)
  digitalWrite(PIN_SOLENOID, LOW);
  Serial.println("System ready. Unlocked.");
}

void loop() {
  // Buttons are active LOW with INPUT_PULLUP
  bool lockPressed   = (digitalRead(PIN_LOCK_BTN)   == LOW);
  bool unlockPressed = (digitalRead(PIN_UNLOCK_BTN) == LOW);

  if (lockPressed && !isLocked) {
    isLocked = true;
    digitalWrite(PIN_SOLENOID, HIGH);
    Serial.println("LOCKED");
    delay(300); // debounce
  }

  if (unlockPressed && isLocked) {
    isLocked = false;
    digitalWrite(PIN_SOLENOID, LOW);
    Serial.println("UNLOCKED");
    delay(300); // debounce
  }
}
