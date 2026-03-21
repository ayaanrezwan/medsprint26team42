// ═══════════════════════════════════════════════════════════════════
//  LIMB-GIRDLE BRACE CONTROLLER  —  ESP32 Firmware (Servo + Tongue)
//  Libraries needed: ESP32Servo, ArduinoJson, WebSockets
//  Install all three via Arduino IDE → Library Manager
// ═══════════════════════════════════════════════════════════════════

#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebSocketsServer.h>

// ── WiFi (for dashboard) ───────────────────────────────────────────
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ── Pin assignments ────────────────────────────────────────────────
#define SERVO_PIN      13   // PWM-capable GPIO → servo signal wire
#define TONGUE_PIN_L   34   // Analog: left pressure pad  → LOCK
#define TONGUE_PIN_R   35   // Analog: right pressure pad → UNLOCK
#define TONGUE_PIN_C   32   // Analog: center pad         → TOGGLE
#define STATUS_LED      2   // Built-in LED

// ── Servo positions ────────────────────────────────────────────────
// Adjust these two angles after testing with your physical brace.
// 0°  = fully extended/stiff  (locked)
// 90° = free range of motion  (unlocked)
#define SERVO_LOCKED    0
#define SERVO_UNLOCKED  90

// ── Tongue sensor tuning ───────────────────────────────────────────
// ADC range: 0–4095. Print raw values first, then set threshold
// just above the resting (no-press) value you observe.
#define PRESS_THRESHOLD  2000   // above this = "pressed"
#define HOLD_MS           300   // must hold this long to confirm press
                                // prevents accidental triggers

// ── State ─────────────────────────────────────────────────────────
Servo braceServo;
bool  braceLocked     = false;
bool  actionTaken     = false;   // prevents repeated triggers per hold
unsigned long holdStart = 0;
bool  wasPressed        = false;

WebSocketsServer ws = WebSocketsServer(81);

// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);

  // Attach servo and start in unlocked position
  braceServo.attach(SERVO_PIN);
  setServo(false);
  Serial.println("Servo ready — starting UNLOCKED");

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.print("\nDashboard: http://");
  Serial.println(WiFi.localIP());

  ws.begin();
  ws.onEvent(onWebSocketEvent);
}

// ─────────────────────────────────────────────────────────────────
void loop() {
  ws.loop();

  // Read all three tongue pads
  int L = analogRead(TONGUE_PIN_L);
  int R = analogRead(TONGUE_PIN_R);
  int C = analogRead(TONGUE_PIN_C);

  bool leftPressed   = L > PRESS_THRESHOLD;
  bool rightPressed  = R > PRESS_THRESHOLD;
  bool centerPressed = C > PRESS_THRESHOLD;
  bool anyPressed    = leftPressed || rightPressed || centerPressed;

  // ── Debounce + hold detection ──────────────────────────────────
  if (anyPressed && !wasPressed) {
    holdStart  = millis();
    wasPressed = true;
    actionTaken = false;
  }
  if (!anyPressed) {
    wasPressed  = false;
    actionTaken = false;
  }

  bool confirmed = wasPressed &&
                   !actionTaken &&
                   (millis() - holdStart > HOLD_MS);

  if (confirmed) {
    actionTaken = true;  // only act once per continuous press

    if (leftPressed)        { setBrace(true,  "TONGUE_LEFT");   }
    else if (rightPressed)  { setBrace(false, "TONGUE_RIGHT");  }
    else if (centerPressed) { setBrace(!braceLocked, "TOGGLE"); }
  }

  // Send status to dashboard every 50ms
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 50) {
    sendStatus(L, R, C);
    lastSend = millis();
  }

  delay(10);
}

// ─────────────────────────────────────────────────────────────────
//  BRACE CONTROL
// ─────────────────────────────────────────────────────────────────
void setBrace(bool lock, const char* reason) {
  braceLocked = lock;
  setServo(lock);
  digitalWrite(STATUS_LED, lock ? HIGH : LOW);
  Serial.printf("%s [%s]\n", lock ? "🔒 LOCKED" : "🔓 Unlocked", reason);
}

void setServo(bool lock) {
  braceServo.write(lock ? SERVO_LOCKED : SERVO_UNLOCKED);
}

// ─────────────────────────────────────────────────────────────────
//  WEBSOCKET — send JSON to dashboard
// ─────────────────────────────────────────────────────────────────
void sendStatus(int L, int R, int C) {
  StaticJsonDocument<128> doc;
  doc["locked"] = braceLocked;
  doc["t1"] = L;
  doc["t2"] = R;
  doc["t3"] = C;
  String json;
  serializeJson(doc, json);
  ws.broadcastTXT(json);
}

// ─────────────────────────────────────────────────────────────────
//  WEBSOCKET — receive commands from dashboard buttons
// ─────────────────────────────────────────────────────────────────
void onWebSocketEvent(uint8_t num, WStype_t type,
                      uint8_t* payload, size_t length) {
  if (type == WStype_TEXT) {
    String msg = String((char*)payload);
    if (msg == "LOCK")   setBrace(true,  "DASHBOARD");
    if (msg == "UNLOCK") setBrace(false, "DASHBOARD");
  }
}#define GYRO_FALL_THRESHOLD   300.0  // deg/s — fast rotation = stumble
#define LOCK_HOLD_MS          2000   // keep brace locked for 2 seconds

// ── State variables ───────────────────────────────────────────────
bool   braceLocked     = false;
bool   manualOverride  = false;
unsigned long lockStartTime = 0;
unsigned long tongueHoldStart = 0;
bool   tongueWasPressed = false;

// ── WebSocket server (port 81) ─────────────────────────────────────
WebSocketsServer ws = WebSocketsServer(81);

// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Pins
  pinMode(LOCK_PIN,   OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(LOCK_PIN, LOW);   // start unlocked

  // IMU init (BNO085 over I2C)
  Wire.begin();
  if (!imu.begin_I2C()) {
    Serial.println("ERROR: IMU not found — check wiring");
    while (1) delay(100);
  }
  // Enable accelerometer + gyroscope at 100Hz
  imu.enableReport(SH2_ACCELEROMETER,      10000);  // 10000µs = 100Hz
  imu.enableReport(SH2_GYROSCOPE_CALIBRATED, 10000);
  Serial.println("IMU ready");

  // WiFi + WebSocket
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.print("\nDashboard at: http://");
  Serial.println(WiFi.localIP());
  ws.begin();
  ws.onEvent(onWebSocketEvent);
}

// ─────────────────────────────────────────────────────────────────
void loop() {
  ws.loop();   // keep WebSocket connections alive

  // 1. Read IMU
  float ax = 0, ay = 0, az = 0;
  float gx = 0, gy = 0, gz = 0;
  readIMU(ax, ay, az, gx, gy, gz);

  // 2. Read tongue sensors
  int t1 = analogRead(TONGUE_PIN_1);
  int t2 = analogRead(TONGUE_PIN_2);
  int t3 = analogRead(TONGUE_PIN_3);
  handleTongueInput(t1, t2, t3);

  // 3. Fall prediction
  if (!braceLocked) {
    bool fallDetected = detectFall(ax, ay, az, gx, gy, gz);
    if (fallDetected) {
      lockBrace("FALL_PREDICTED");
    }
  }

  // 4. Auto-unlock after hold time (unless user re-locked manually)
  if (braceLocked && !manualOverride) {
    if (millis() - lockStartTime > LOCK_HOLD_MS) {
      unlockBrace();
    }
  }

  // 5. Send data to dashboard every 50ms
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 50) {
    sendDashboardData(ax, ay, az, gx, gy, gz, t1, t2, t3);
    lastSend = millis();
  }

  delay(10);  // 100Hz loop
}

// ─────────────────────────────────────────────────────────────────
//  IMU READER
// ─────────────────────────────────────────────────────────────────
void readIMU(float &ax, float &ay, float &az,
             float &gx, float &gy, float &gz) {
  if (imu.getSensorEvent(&imuValue)) {
    if (imuValue.sensorId == SH2_ACCELEROMETER) {
      ax = imuValue.un.accelerometer.x;
      ay = imuValue.un.accelerometer.y;
      az = imuValue.un.accelerometer.z;
    }
    if (imuValue.sensorId == SH2_GYROSCOPE_CALIBRATED) {
      gx = imuValue.un.gyroscope.x * 57.3;  // rad/s → deg/s
      gy = imuValue.un.gyroscope.y * 57.3;
      gz = imuValue.un.gyroscope.z * 57.3;
    }
  }
}

// ─────────────────────────────────────────────────────────────────
//  FALL DETECTION  (threshold-based — good enough for a hackathon)
//  Upgrade: replace with LSTM inference using EloquentML library
// ─────────────────────────────────────────────────────────────────
bool detectFall(float ax, float ay, float az,
                float gx, float gy, float gz) {
  // Total acceleration magnitude (subtract gravity baseline ~9.81)
  float accelMag = sqrt(ax*ax + ay*ay + az*az);
  float gyroMag  = sqrt(gx*gx + gy*gy + gz*gz);

  // Fall signature: sudden large acceleration AND fast rotation
  bool suspiciousAccel = accelMag > ACCEL_FALL_THRESHOLD;
  bool suspiciousGyro  = gyroMag  > GYRO_FALL_THRESHOLD;

  if (suspiciousAccel && suspiciousGyro) {
    Serial.printf("⚠ Fall signal! accel=%.1f gyro=%.1f\n",
                  accelMag, gyroMag);
    return true;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────
//  TONGUE SENSOR INPUT HANDLER
//  Single-press left = lock | Single-press right = unlock
//  Press center = toggle (for demo purposes)
// ─────────────────────────────────────────────────────────────────
void handleTongueInput(int t1, int t2, int t3) {
  bool leftPressed   = t1 > TONGUE_PRESS_THRESHOLD;
  bool rightPressed  = t2 > TONGUE_PRESS_THRESHOLD;
  bool centerPressed = t3 > TONGUE_PRESS_THRESHOLD;
  bool anyPressed    = leftPressed || rightPressed || centerPressed;

  // Debounce: require hold for TONGUE_HOLD_MS
  if (anyPressed && !tongueWasPressed) {
    tongueHoldStart = millis();
    tongueWasPressed = true;
  }
  if (!anyPressed) {
    tongueWasPressed = false;
  }

  bool confirmed = tongueWasPressed &&
                   (millis() - tongueHoldStart > TONGUE_HOLD_MS);
  if (!confirmed) return;

  // Act on the confirmed press (only once per hold)
  static bool actionTaken = false;
  if (confirmed && !actionTaken) {
    actionTaken = true;
    if (leftPressed) {
      lockBrace("MANUAL");
      manualOverride = true;
    } else if (rightPressed) {
      unlockBrace();
      manualOverride = false;
    } else if (centerPressed) {
      braceLocked ? unlockBrace() : lockBrace("MANUAL");
    }
  }
  if (!anyPressed) actionTaken = false;
}

// ─────────────────────────────────────────────────────────────────
//  BRACE ACTUATOR CONTROL
// ─────────────────────────────────────────────────────────────────
void lockBrace(const char* reason) {
  braceLocked   = true;
  lockStartTime = millis();
  digitalWrite(LOCK_PIN, HIGH);
  digitalWrite(STATUS_LED, HIGH);
  Serial.printf("🔒 LOCKED  [%s]\n", reason);
}

void unlockBrace() {
  braceLocked  = false;
  digitalWrite(LOCK_PIN, LOW);
  digitalWrite(STATUS_LED, LOW);
  Serial.println("🔓 Unlocked");
}

// ─────────────────────────────────────────────────────────────────
//  WEBSOCKET — send JSON to dashboard
// ─────────────────────────────────────────────────────────────────
void sendDashboardData(float ax, float ay, float az,
                       float gx, float gy, float gz,
                       int t1,  int t2,  int t3) {
  StaticJsonDocument<256> doc;
  doc["ax"] = ax;  doc["ay"] = ay;  doc["az"] = az;
  doc["gx"] = gx;  doc["gy"] = gy;  doc["gz"] = gz;
  doc["t1"] = t1;  doc["t2"] = t2;  doc["t3"] = t3;
  doc["locked"]  = braceLocked;
  doc["accelMag"] = sqrt(ax*ax + ay*ay + az*az);
  doc["gyroMag"]  = sqrt(gx*gx + gy*gy + gz*gz);
  String json;
  serializeJson(doc, json);
  ws.broadcastTXT(json);
}

// ─────────────────────────────────────────────────────────────────
//  WEBSOCKET EVENT — handle commands sent from dashboard
// ─────────────────────────────────────────────────────────────────
void onWebSocketEvent(uint8_t num, WStype_t type,
                      uint8_t* payload, size_t length) {
  if (type == WStype_TEXT) {
    String msg = String((char*)payload);
    if (msg == "LOCK")   { lockBrace("DASHBOARD"); manualOverride = true; }
    if (msg == "UNLOCK") { unlockBrace(); manualOverride = false; }
  }
}
