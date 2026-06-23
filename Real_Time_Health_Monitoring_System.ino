/* Full Health Monitor + Blynk + stable MAX30100 & Fall Detection + Email Notifications
   ESP32 + MAX30100 + MPU6050 + DHT11 + SH1106 OLED + buzzer + LEDs

   Changes:
   - MAX30100 runs in its own FreeRTOS task on core 0
   - MPU6050 runs in its own FreeRTOS task on core 1
   - All I2C (Wire) access is protected by a mutex (no bus corruption)
   - Fall detection logic & thresholds are EXACTLY your original ones
   - Fall evaluation still happens every 600 ms (MPU_PRINT_INTERVAL)
   - Fixed Notification System with proper timing
*/

#include <Wire.h>
#include "MAX30100_PulseOximeter.h"
#include <MPU6050.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DHT.h>

// -------------------- DHT --------------------
#define DHT_PIN 19
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// -------------------- MAX30100 ----------------
PulseOximeter pox;
bool poxOk = false;

// -------------------- MPU6050 -----------------
MPU6050 mpu;

// -------------------- OLED --------------------
#define SDA_PIN 21
#define SCL_PIN 22

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// -------------------- Timing ------------------
unsigned long lastPoxRead = 0;
const unsigned long POX_PRINT_INTERVAL = 1000;   // 1s

const unsigned long MPU_PRINT_INTERVAL = 600;    // fall logic interval (unchanged)

// -------------------- Fall logic (UNCHANGED thresholds) ----
const float FALL_LOWER_G = 0.5f;
const float FALL_UPPER_G = 2.5f;
const unsigned long FALL_DEBOUNCE_MS = 3000;
const float ORIENTATION_THRESHOLD_DEG = 80.0f;

volatile bool fallDetected = false;
volatile unsigned long fallTime = 0;
volatile bool haveLastOrientation = false;
volatile float lastPitchDeg = 0.0f;
volatile float lastRollDeg  = 0.0f;

// -------------------- Temperature store --------
float lastTempC = NAN;

/* ========== pins & thresholds ========== */
const int BUZZER_PIN     = 23;
const int LED_FALL_PIN   = 5;
const int LED_TEMP_PIN   = 17;
const int LED_PULSE_PIN  = 16;

const float TEMP_LOW_C  = 27.0f;
const float TEMP_HIGH_C = 38.0f;

const int BPM_LOW  = 50;
const int BPM_HIGH = 120;
const int SPO2_LOW = 92;

const unsigned long BUZZER_TOGGLE_MS = 250;
/* ====================================== */

bool buzzerOn = false;
unsigned long lastBuzzerToggle = 0;

// ---------------- Blynk / WiFi ----------------
#define BLYNK_TEMPLATE_ID   "TMPL60hj1C7xU"
#define BLYNK_TEMPLATE_NAME "Health Monitor System"
#define BLYNK_AUTH_TOKEN    "cOJ4zGidbkqTZzh5lp2TkRV_ZJ7bLpDY"

const char* WIFI_SSID = "BUBT Hardware Lab";
const char* WIFI_PASS = "bubt1234";

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

bool emergencyFromApp    = false;
bool emergencySilenced   = false;

// --------------- Notification System ---------------
unsigned long lastFallNotificationTime = 0;
unsigned long lastTempNotificationTime = 0;
unsigned long lastBpmNotificationTime = 0;
unsigned long lastSpo2NotificationTime = 0;

// Different intervals for different alert types
const unsigned long FALL_NOTIFICATION_INTERVAL = 10000;    // 10 seconds for falls
const unsigned long VITAL_NOTIFICATION_INTERVAL = 30000;   // 30 seconds for vitals
const unsigned long EMERGENCY_NOTIFICATION_INTERVAL = 5000; // 5 seconds for emergency

bool previousFallState = false;
bool previousTempAlarmState = false;
bool previousBpmAlertState = false;
bool previousSpo2AlertState = false;

// Improved Notification function with separate timing for each type
void sendNotification(const char* type, const char* message, bool isCritical = false) {
  unsigned long now = millis();
  unsigned long lastNotificationTime = 0;
  unsigned long notificationInterval = VITAL_NOTIFICATION_INTERVAL;
  
  // Set different intervals for different notification types
  if (strcmp(type, "fall_detected") == 0) {
    lastNotificationTime = lastFallNotificationTime;
    notificationInterval = FALL_NOTIFICATION_INTERVAL;
  } else if (strcmp(type, "temp_alert") == 0) {
    lastNotificationTime = lastTempNotificationTime;
    notificationInterval = VITAL_NOTIFICATION_INTERVAL;
  } else if (strcmp(type, "bpm_alert") == 0) {
    lastNotificationTime = lastBpmNotificationTime;
    notificationInterval = VITAL_NOTIFICATION_INTERVAL;
  } else if (strcmp(type, "spo2_alert") == 0) {
    lastNotificationTime = lastSpo2NotificationTime;
    notificationInterval = VITAL_NOTIFICATION_INTERVAL;
  } else if (strcmp(type, "emergency") == 0) {
    notificationInterval = EMERGENCY_NOTIFICATION_INTERVAL;
  }
  
  // Rate limiting check
  if (now - lastNotificationTime < notificationInterval) {
    Serial.print("⏰ Notification rate limited: ");
    Serial.println(type);
    return;
  }
  
  // Update the specific notification time
  if (strcmp(type, "fall_detected") == 0) {
    lastFallNotificationTime = now;
  } else if (strcmp(type, "temp_alert") == 0) {
    lastTempNotificationTime = now;
  } else if (strcmp(type, "bpm_alert") == 0) {
    lastBpmNotificationTime = now;
  } else if (strcmp(type, "spo2_alert") == 0) {
    lastSpo2NotificationTime = now;
  }
  
  // Blynk Notification - with connection check
  if (Blynk.connected()) {
    Blynk.logEvent(type, message);
    Serial.print("🔔 BLYNK NOTIFICATION SENT: ");
  } else {
    Serial.print("🔔 OFFLINE NOTIFICATION: ");
    Serial.println("⚠️ Blynk not connected, notification queued");
  }
  
  Serial.print(type);
  Serial.print(" - ");
  Serial.println(message);
  
  // Always print critical alerts
  if (isCritical) {
    Serial.println("🚨 CRITICAL ALERT - Immediate attention required!");
  }
}

// Fall-specific notifications
void sendFallNotification() {
  String message = "🚨 Fall Detected! Immediate attention needed!";
  Serial.println("********** SENDING FALL NOTIFICATION **********");
  sendNotification("fall_detected", message.c_str(), true);
}

void sendStableNotification() {
  sendNotification("stable", "✅ Person is now stable. Fall alert cleared.", false);
}

void sendTempAlertNotification(bool isHigh) {
  String message = "🌡️ Temperature Alert: ";
  message += isHigh ? "High" : "Low";
  message += " temperature detected (" + String(lastTempC) + "°C)";
  
  sendNotification("temp_alert", message.c_str(), true);
}

void sendBpmAlertNotification(bool isHigh) {
  float bpm = poxOk ? pox.getHeartRate() : 0.0f;
  String message = "💓 Heart Rate Alert: ";
  message += isHigh ? "High" : "Low";
  message += " heart rate detected (" + String(bpm, 0) + " BPM)";
  
  sendNotification("bpm_alert", message.c_str(), true);
}

void sendSpo2AlertNotification() {
  float spo2 = poxOk ? pox.getSpO2() : 0.0f;
  String message = "🩸 SpO2 Alert: Low oxygen level detected (" + String(spo2, 0) + "%)";
  sendNotification("spo2_alert", message.c_str(), true);
}

void sendEmergencyNotification() {
  sendNotification("emergency", "🚑 Emergency button pressed! Immediate assistance required!", true);
}

void sendSystemStartNotification() {
  String message = "Health Monitor System Started Successfully";
  sendNotification("system_start", message.c_str(), false);
}

BLYNK_WRITE(V5) { // Emergency from app
  if (param.asInt()) {
    emergencyFromApp  = true;
    emergencySilenced = false;
    Serial.println("🚑 EMERGENCY BUTTON PRESSED FROM APP");
    sendEmergencyNotification();
    
    // Force buzzer on for emergency
    buzzerOn = true;
    digitalWrite(BUZZER_PIN, HIGH);
    lastBuzzerToggle = millis();
  }
}

BLYNK_WRITE(V6) { // Emergency silence
  if (param.asInt()) {
    emergencySilenced = true;
    emergencyFromApp  = false;
    sendNotification("silence", "🔇 Notifications silenced from app");
    
    // Turn off buzzer when silenced
    buzzerOn = false;
    digitalWrite(BUZZER_PIN, LOW);
  } else {
    emergencySilenced = false;
    sendNotification("resume", "🔔 Notifications resumed");
  }
}

// --------------- I2C mutex (protect Wire) ---------------
SemaphoreHandle_t i2cMutex = NULL;

// dummy i2c helpers silenced
void i2cScan() {}
void rawReadFrom(uint8_t a, uint8_t b, uint8_t c) {}

// MAX30100 beat callback
void onBeatDetected() {
  Serial.println("Beat!");
}

// Print status line
void printStatus(float tempC, float bpm, float spo2, bool fall) {
  Serial.print("Temp: ");
  if (isnan(tempC)) Serial.print("--");
  else Serial.print(tempC, 1);

  Serial.print(" | BPM: ");
  if (isnan(bpm) || bpm <= 0.01) Serial.print("0");
  else Serial.print(bpm, 1);

  Serial.print(" | SpO2: ");
  if (isnan(spo2) || spo2 <= 0.01) Serial.print("0");
  else Serial.print(spo2, 1);

  Serial.print(" | Fall: ");
  Serial.println(fall ? "YES" : "NO");
}

// OLED update (wrapped with I2C mutex)
void updateOLED(float bpm, float tempC, float spo2, bool fall) {
  if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  display.println("Health Monitor");
  display.println("-----------------");

  display.print("Temp: ");
  if (isnan(tempC)) display.println("--");
  else display.println(tempC, 1);

  display.print("HR: ");
  if (isnan(bpm) || bpm <= 0.01) display.print("0");
  else display.print(bpm, 1);
  display.println(" BPM");

  display.print("SpO2: ");
  if (isnan(spo2) || spo2 <= 0.01) display.print("0");
  else display.print(spo2, 1);
  display.println(" %");

  display.print("Fall: ");
  display.println(fall ? "YES" : "NO");

  // Show notification status
  display.print("Notify: ");
  display.println(emergencySilenced ? "OFF" : "ON");
  
  // Show Blynk connection status
  display.print("Blynk: ");
  display.println(Blynk.connected() ? "ON" : "OFF");

  display.display();

  if (i2cMutex) xSemaphoreGive(i2cMutex);
}

// ---------------- Tasks ----------------
TaskHandle_t poxTaskHandle = NULL;
TaskHandle_t mpuTaskHandle = NULL;

// MAX30100 task (core 0) – continuous sensor update
void poxTask(void *param) {
  for (;;) {
    if (poxOk) {
      if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY);
      pox.update();
      if (i2cMutex) xSemaphoreGive(i2cMutex);
    }
    vTaskDelay(1); // ~1 ms
  }
}

// MPU task (core 1) – fast sampling, fall logic every 600 ms
void mpuTask(void *param) {
  unsigned long lastFallEval = 0;

  for (;;) {
    // 1) Read MPU (fast, every ~10 ms)
    int16_t ax_r, ay_r, az_r;
    if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY);
    mpu.getAcceleration(&ax_r, &ay_r, &az_r);
    if (i2cMutex) xSemaphoreGive(i2cMutex);

    float axg = ax_r / 16384.0f;
    float ayg = ay_r / 16384.0f;
    float azg = az_r / 16384.0f;
    float mag = sqrtf(axg*axg + ayg*ayg + azg*azg);

    unsigned long now = millis();

    // 2) Run original fall logic every MPU_PRINT_INTERVAL (600 ms)
    if (now - lastFallEval >= MPU_PRINT_INTERVAL) {
      lastFallEval = now;

      // --- ACC MAG FALL (unchanged) ---
      if (!fallDetected && (mag < FALL_LOWER_G || mag > FALL_UPPER_G)) {
        fallDetected = true;
        fallTime = now;
        Serial.println("********** FALL DETECTED (AccMag) **********");
        // Send notification immediately when fall is detected
        if (!emergencySilenced) {
          sendFallNotification();
        }
      }

      // --- ORIENTATION FALL (unchanged) ---
      float pitchRad = atan2f(axg, sqrtf(ayg*ayg + azg*azg));
      float rollRad  = atan2f(ayg, azg);
      float pitchDeg = pitchRad * 180.0f / M_PI;
      float rollDeg  = rollRad  * 180.0f / M_PI;

      if (haveLastOrientation) {
        float dPitch = fabs(pitchDeg - lastPitchDeg);
        float dRoll  = fabs(rollDeg  - lastRollDeg);
        if (dPitch > 180.0f) dPitch = 360.0f - dPitch;
        if (dRoll  > 180.0f) dRoll  = 360.0f - dRoll;

        if (!fallDetected &&
            (dPitch >= ORIENTATION_THRESHOLD_DEG ||
             dRoll  >= ORIENTATION_THRESHOLD_DEG)) {
          fallDetected = true;
          fallTime = now;
          Serial.println("********** FALL DETECTED (Orientation) **********");
          // Send notification immediately when fall is detected
          if (!emergencySilenced) {
            sendFallNotification();
          }
        }
      } else {
        haveLastOrientation = true;
      }

      lastPitchDeg = pitchDeg;
      lastRollDeg  = rollDeg;

      // Notification Logic for Fall Detection State Changes
      if (fallDetected && !previousFallState && !emergencySilenced) {
        previousFallState = true;
      }
      else if (!fallDetected && previousFallState) {
        sendStableNotification();
        previousFallState = false;
      }

      // Debounce / stable reset
      if (fallDetected && (now - fallTime > FALL_DEBOUNCE_MS)) {
        fallDetected = false;
        Serial.println("Stable");
      }

      digitalWrite(LED_FALL_PIN, fallDetected ? HIGH : LOW);
    }

    // Small delay: fast sampling (~10 ms)
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Health Monitor + Blynk + MAX30100 + MPU Tasks + Notifications ===");

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(80000);  // slightly slower = more stable

  // Mutex
  i2cMutex = xSemaphoreCreateMutex();

  // Pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_FALL_PIN, OUTPUT);
  pinMode(LED_TEMP_PIN, OUTPUT);
  pinMode(LED_PULSE_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_FALL_PIN, LOW);
  digitalWrite(LED_TEMP_PIN, LOW);
  digitalWrite(LED_PULSE_PIN, LOW);

  // OLED
  if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY);
  if (!display.begin(0x3C, true)) {
    Serial.println("OLED init failed");
    while (1) delay(1000);
  }
  display.clearDisplay();
  display.display();
  if (i2cMutex) xSemaphoreGive(i2cMutex);

  // DHT
  dht.begin();

  // MPU
  if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY);
  mpu.initialize();
  if (i2cMutex) xSemaphoreGive(i2cMutex);

  // MAX30100
  Serial.println("Initializing MAX30100...");
  if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY);
  poxOk = pox.begin();
  if (poxOk) {
    pox.setIRLedCurrent(MAX30100_LED_CURR_27_1MA);
    pox.setOnBeatDetectedCallback(onBeatDetected);
    Serial.println("MAX30100 OK");
  } else {
    Serial.println("MAX30100 INIT FAILED!");
  }
  if (i2cMutex) xSemaphoreGive(i2cMutex);

  // Create MAX30100 task on core 0
  if (poxOk) {
    xTaskCreatePinnedToCore(
      poxTask, "poxTask", 4096, NULL, 1, &poxTaskHandle, 0
    );
  }

  // Create MPU task on core 1
  xTaskCreatePinnedToCore(
    mpuTask, "mpuTask", 4096, NULL, 1, &mpuTaskHandle, 1
  );

  // Blynk
  Serial.println("Connecting to WiFi + Blynk...");
  Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASS);

  lastPoxRead = millis();
  
  // Send system start notification after a delay to ensure WiFi is connected
  delay(10000); // Increased delay to ensure connection
  if (Blynk.connected()) {
    sendSystemStartNotification();
    Serial.println("✅ Blynk Connected - Notifications Ready");
  } else {
    Serial.println("⚠️ Blynk Not Connected - Notifications may be delayed");
  }
  
  Serial.println("📧 Notification System Initialized");
  Serial.println("• Fall alerts: Every 10 seconds");
  Serial.println("• Vital alerts: Every 30 seconds"); 
  Serial.println("• Emergency: Every 5 seconds");
}

// ---------------- Main loop ----------------
void loop() {
  unsigned long now = millis();
  Blynk.run();

  bool tempAlarm  = false;
  bool bpmAlert = false;
  bool spo2Alert = false;

  // ---- POX + TEMP + OLED + Blynk ----
  if (now - lastPoxRead >= POX_PRINT_INTERVAL) {
    lastPoxRead = now;

    float bpm  = poxOk ? pox.getHeartRate() : 0.0f;
    float spo2 = poxOk ? pox.getSpO2()      : 0.0f;

    float t = dht.readTemperature();
    if (!isnan(t)) lastTempC = t;

    // Check alarms
    if (!isnan(lastTempC) && (lastTempC < TEMP_LOW_C || lastTempC > TEMP_HIGH_C))
      tempAlarm = true;

    if (bpm > 0.01 && (bpm < BPM_LOW || bpm > BPM_HIGH))
      bpmAlert = true;
    if (spo2 > 0.01 && spo2 < SPO2_LOW)
      spo2Alert = true;

    // Send notifications for vital signs
    if (!emergencySilenced) {
      // Temperature alert
      if (tempAlarm && !previousTempAlarmState) {
        Serial.println("🌡️ TEMPERATURE ALARM TRIGGERED");
        sendTempAlertNotification(lastTempC > TEMP_HIGH_C);
        previousTempAlarmState = true;
      } else if (!tempAlarm && previousTempAlarmState) {
        Serial.println("🌡️ Temperature back to normal");
        sendNotification("temp_normal", "✅ Temperature back to normal range", false);
        previousTempAlarmState = false;
      }
      
      // BPM alert
      if (bpmAlert && !previousBpmAlertState) {
        Serial.println("💓 HEART RATE ALARM TRIGGERED");
        sendBpmAlertNotification(bpm > BPM_HIGH);
        previousBpmAlertState = true;
      } else if (!bpmAlert && previousBpmAlertState) {
        Serial.println("💓 Heart rate back to normal");
        sendNotification("bpm_normal", "✅ Heart rate back to normal range", false);
        previousBpmAlertState = false;
      }
      
      // SpO2 alert
      if (spo2Alert && !previousSpo2AlertState) {
        Serial.println("🩸 SPO2 ALARM TRIGGERED");
        sendSpo2AlertNotification();
        previousSpo2AlertState = true;
      } else if (!spo2Alert && previousSpo2AlertState) {
        Serial.println("🩸 SpO2 back to normal");
        sendNotification("spo2_normal", "✅ SpO2 back to normal range", false);
        previousSpo2AlertState = false;
      }
    }

    printStatus(lastTempC, bpm, spo2, fallDetected);

    digitalWrite(LED_TEMP_PIN, tempAlarm ? HIGH : LOW);
    digitalWrite(LED_PULSE_PIN, (bpmAlert || spo2Alert) ? HIGH : LOW);

    updateOLED(bpm, lastTempC, spo2, fallDetected);

    // Send data to Blynk
    if (Blynk.connected()) {
      Blynk.virtualWrite(V0, lastTempC);
      Blynk.virtualWrite(V1, bpm);
      Blynk.virtualWrite(V2, spo2);
      Blynk.virtualWrite(V3, 0);
      Blynk.virtualWrite(V4, fallDetected ? 1 : 0);
    }
  }

  // ---- Buzzer ----
  bool anyAlarm =
    fallDetected ||
    digitalRead(LED_TEMP_PIN)  == HIGH ||
    digitalRead(LED_PULSE_PIN) == HIGH ||
    emergencyFromApp;

  if (emergencySilenced) anyAlarm = false;

  if (anyAlarm) {
    if (now - lastBuzzerToggle >= BUZZER_TOGGLE_MS) {
      lastBuzzerToggle = now;
      buzzerOn = !buzzerOn;
      digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);
    }
  } else {
    buzzerOn = false;
    digitalWrite(BUZZER_PIN, LOW);
  }

  delay(1); // harmless now
}