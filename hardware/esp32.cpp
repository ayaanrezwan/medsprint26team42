// ═══════════════════════════════════════════════════════════════════
//  LIMB-GIRDLE BRACE CONTROLLER  —  ESP32 Firmware
//  Flash with Arduino IDE or PlatformIO
//  Libraries needed: Adafruit_BNO08x, ArduinoJson, WiFi (built-in)
// ═══════════════════════════════════════════════════════════════════

#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebSocketsServer.h>

// ── WiFi credentials (for dashboard) ──────────────────────────────
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ── Pin assignments ────────────────────────────────────────────────
#define LOCK_PIN        18   // GPIO pin → relay/solenoid for knee lock
#define TONGUE_PIN_1    34   // Analog in: tongue pressure sensor 1 (left)
#define TONGUE_PIN_2    35   // Analog in: tongue pressure sensor 2 (right)
#define TONGUE_PIN_3    32   // Analog in: tongue pressure sensor 3 (center)
#define STATUS_LED      2    // Built-in LED: blink on fall detection

// ── Tongue press thresholds ────────────────────────────────────────
// ADC reads 0–4095. Adjust these after testing your actual retainer.
#define TONGUE_PRESS_THRESHOLD  2000   // value above = "pressed"
#define TONGUE_HOLD_MS          300    // must hold for 300ms to count

// ── IMU settings ──────────────────────────────────────────────────
Adafruit_BNO08x imu;
sh2_SensorValue_t imuValue;

// ── Fall detection settings ───────────────────────────────────────
// These are the threshold-based rules (no ML needed for hackathon).
// Tune by printing raw values and walking around first.
#define ACCEL_FALL_THRESHOLD  18.0   // m/s² — sudden spike = impact
#define GYRO_FALL_THRESHOLD   300.0  // deg/s — fast rotation = stumble
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
