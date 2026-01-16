#include <WiFiS3.h>
#include <Wire.h>

// ----------- WiFi credentials -----------
const char* ssid = "MiFibra-9A54";
const char* password = "R9Jytzyw";

// ----------- ThingSpeak -----------
const char* server = "api.thingspeak.com";
String apiKey = "NZDD8UEISXDDIT6E";

// ----------- Pins -----------
const int micPin = A0;       // KY-037 AO
const int lightPin = A1;     // LDR midpoint (divider)
const int buttonPin = 2;     // Button to GND (INPUT_PULLUP)
const int ledPin = 13;       // LED
const int buzzerPin = 12;    // Buzzer

// ----------- Thresholds -----------
int soundThreshold = 600;    // we'll calibrate later
int lightThreshold = 100;    // dark environment threshold
int peakSound = 0;           // (no lo uso aquí porque tú aún no lo estabas usando)

WiFiClient client;

// ----------- Double press + latch -----------
bool panicLatched = false;
int pressCount = 0;
unsigned long firstPressTime = 0;
bool lastButtonState = false;
const unsigned long doublePressWindow = 1200; // ms

// ----------- Hold to cancel -----------
unsigned long buttonHoldStart = 0;
const unsigned long holdToCancelMs = 3000; // 3 seconds hold to cancel

// ----------- Buzzer control -----------
bool buzzerAlreadyBeeped = false;

// ----------- Send interval -----------
unsigned long lastSend = 0;
const unsigned long sendIntervalMs = 15000;

// ----------- Buzzer pattern -----------
void beepPatternSOS(int pin) {
  // 3 beeps + 0.5s + 3 beeps + 0.5s + 3 beeps
  for (int block = 0; block < 3; block++) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(pin, HIGH);
      delay(150);
      digitalWrite(pin, LOW);
      delay(150);
    }
    if (block < 2) delay(500);
  }
}

/* =========================================================
   ===============   MPU6050 (ADDED)   ======================
   ========================================================= */

// Tu módulo responde en 0x68 pero WHO_AM_I = 0x70 (clon/variante),
// así que lo leemos "a pelo" por I2C para que sea robusto.
const byte MPU_ADDR = 0x68;

// Muestreo IMU ~20 Hz
unsigned long lastImuSample = 0;
const unsigned long imuSampleIntervalMs = 50;

// ---- Free-fall + impact detection (CALIBRATED) ----
// From your logs: normal ~260M-313M, free-fall can drop to a few million,
// impact spikes > ~600M and often > 1B.
const uint64_t FREEFALL_MAG2 = 150000000ULL;   // 150M
const uint64_t IMPACT_MAG2   = 500000000ULL;   // 500M
const unsigned long FALL_WINDOW_MS = 1500;     // 1.5s

// Free-fall state machine
bool freeFallSeen = false;
unsigned long freeFallTime = 0;

// Optional cooldown to avoid repeated triggers from one event
unsigned long fallCooldownUntil = 0;
const unsigned long fallCooldownMs = 4000; // 4s

// Latch para que el evento de caída no dure solo 50ms
bool fallLatched = false;
unsigned long fallLatchedTime = 0;
const unsigned long fallLatchDurationMs = 15000; // 15s

void mpuWriteReg(byte reg, byte val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission(true);
}

int16_t mpuRead16(byte reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, (byte)2);

  byte hi = 0, lo = 0;
  if (Wire.available()) hi = Wire.read();
  if (Wire.available()) lo = Wire.read();
  return (int16_t)((hi << 8) | lo);
}

void initMPU() {
  Wire.begin();
  Wire.setClock(100000);

  // Wake up
  mpuWriteReg(0x6B, 0x00);

  // Accel range ±2g (0x1C = 0x00)
  mpuWriteReg(0x1C, 0x00);
}

/* ========================================================= */

void connectWiFi() {
  Serial.println("Starting WiFi...");
  int status = WL_IDLE_STATUS;

  while (status != WL_CONNECTED) {
    Serial.print("Connecting to SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, password);
    delay(2000);
  }

  Serial.println("✅ WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void sendToThingSpeak(int soundValue, int lightValue, int buttonRaw, int eventCode) {
  Serial.println("Sending to ThingSpeak...");

  if (client.connect(server, 80)) {
    String url = "/update?api_key=" + apiKey +
                 "&field1=" + String(soundValue) +
                 "&field2=" + String(lightValue) +
                 "&field3=" + String(buttonRaw) +
                 "&field4=" + String(eventCode) +
                 "&status=OK";

    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + server + "\r\n" +
                 "Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 7000) {
        Serial.println("❌ Timeout waiting for response.");
        client.stop();
        return;
      }
    }

    String response = client.readString();
    client.stop();

    int idx = response.lastIndexOf("\r\n\r\n");
    if (idx != -1) {
      String body = response.substring(idx + 4);
      body.trim();
      Serial.print("ThingSpeak BODY (update id): ");
      Serial.println(body);
    }

    Serial.println("✅ Request finished.");
  } else {
    Serial.println("❌ Connection to ThingSpeak failed.");
    client.stop();
  }
}

void setup() {
  Serial.begin(9600);
  delay(1000);

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  digitalWrite(ledPin, LOW);
  digitalWrite(buzzerPin, LOW);

  // ✅ MPU6050 init (ADDED)
  initMPU();

  connectWiFi();
}

void loop() {
  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi disconnected. Reconnecting...");
    connectWiFi();
  }

  // Read sensors frequently
  int soundValue = analogRead(micPin);
  int lightValue = analogRead(lightPin);

  // Button raw (pressed=1)
  int buttonRaw = (digitalRead(buttonPin) == LOW) ? 1 : 0;
  bool buttonNow = (buttonRaw == 1);

  // ---- Hold to cancel (3 seconds) ----
  if (buttonNow) {
    if (buttonHoldStart == 0) buttonHoldStart = millis();

    if (panicLatched && (millis() - buttonHoldStart >= holdToCancelMs)) {
      panicLatched = false;
      pressCount = 0;
      buttonHoldStart = 0;
      buzzerAlreadyBeeped = false;
      Serial.println("✅ Panic cancelled (hold).");
    }
  } else {
    buttonHoldStart = 0;
  }

  // ---- Double press detection (fast loop) ----
  if (buttonNow && !lastButtonState) {
    pressCount++;

    if (pressCount == 1) {
      firstPressTime = millis();
    } else if (pressCount == 2) {
      if (millis() - firstPressTime <= doublePressWindow) {
        panicLatched = true;            // ✅ latch ON
        buzzerAlreadyBeeped = false;    // ✅ allow buzzer each activation
      }
      pressCount = 0;
    }
  }

  // Reset if too slow
  if (pressCount == 1 && (millis() - firstPressTime > doublePressWindow)) {
    pressCount = 0;
  }

  lastButtonState = buttonNow;

  /* ================= MPU sampling + fall detect (FREEFALL + IMPACT) ================= */
  if (millis() - lastImuSample >= imuSampleIntervalMs) {
    lastImuSample = millis();

    int16_t ax = mpuRead16(0x3B);
    int16_t ay = mpuRead16(0x3D);
    int16_t az = mpuRead16(0x3F);

    // Use uint64_t to avoid overflow during impact peaks
    uint64_t mag2 =
      (uint64_t)ax * ax +
      (uint64_t)ay * ay +
      (uint64_t)az * az;

    //Serial.print("MAG2=");
    //Serial.println(mag2);

    unsigned long now = millis();

    // Optional cooldown to avoid re-triggering repeatedly
    if (now >= fallCooldownUntil) {

      // 1) Detect free-fall (very low magnitude)
      if (!freeFallSeen && mag2 < FREEFALL_MAG2) {
        freeFallSeen = true;
        freeFallTime = now;
        // Serial.println("Free-fall detected");
      }

      // 2) After free-fall, look for impact within window
      if (freeFallSeen) {

        if (mag2 > IMPACT_MAG2 && (now - freeFallTime) <= FALL_WINDOW_MS) {
          fallLatched = true;
          fallLatchedTime = now;
          buzzerAlreadyBeeped = false;
          Serial.println("⚠️ FALL CONFIRMED (free-fall + impact).");

          // Reset free-fall state + start cooldown
          freeFallSeen = false;
          fallCooldownUntil = now + fallCooldownMs;
        }

        // Timeout: if no impact occurs, reset free-fall state
        if ((now - freeFallTime) > FALL_WINDOW_MS) {
          freeFallSeen = false;
        }
      }
    }
  }

  // Auto-clear fall after a while
  if (fallLatched && (millis() - fallLatchedTime > fallLatchDurationMs)) {
    fallLatched = false;
  }
  /* ===================================================================== */

  // ---- Event code ----
  // 0 = normal
  // 3 = dark environment (LED only)
  // 4 = panic (double press latched)
  // 1 = fall (baseline)
  int eventCode = 0;

  if (lightValue < lightThreshold) eventCode = 3;

  // ✅ Fall has priority over low light
  if (fallLatched) eventCode = 1;

  // ✅ Panic has highest priority
  if (panicLatched) eventCode = 4;

  // ---- LED behaviour ----
  digitalWrite(ledPin, (eventCode != 0) ? HIGH : LOW);

  // ---- Buzzer behaviour ----
  // Only for fall (code 1) or panic (code 4)
  if (eventCode == 1 || eventCode == 4) {
    if (!buzzerAlreadyBeeped) {
      beepPatternSOS(buzzerPin);
      buzzerAlreadyBeeped = true;
    }
  } else {
    buzzerAlreadyBeeped = false;
    digitalWrite(buzzerPin, LOW);
  }

  // ---- Send to ThingSpeak every 25s (without blocking loop) ----
  if (millis() - lastSend >= sendIntervalMs) {
    lastSend = millis();

    Serial.print("Sound: "); Serial.print(soundValue);
    Serial.print(" | Light: "); Serial.print(lightValue);
    Serial.print(" | Button: "); Serial.print(buttonRaw);
    Serial.print(" | Fall_Latched: "); Serial.print(fallLatched);
    Serial.print(" | Panic_Latched: "); Serial.print(panicLatched);
    Serial.print(" | Event: "); Serial.println(eventCode);

    sendToThingSpeak(soundValue, lightValue, buttonRaw, eventCode);
  }

  // Small delay: still fast enough for double press
  delay(10);
}

