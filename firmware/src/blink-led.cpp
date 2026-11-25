#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_RX_BUFFER 1024
#include <TinyGsmClient.h>
#include <ESP_SSLClient.h>
#include <time.h>
#include "tracker_config.h"
#include "tracker_ca.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <TinyGPS++.h>
#include <Wire.h>

constexpr int GPS_RX_PIN = 32;
constexpr int GPS_TX_PIN = 33;
constexpr uint32_t GPS_BAUD = 9600;

constexpr int GSM_RX_PIN = 14; // SIM800L TXD -> ESP32 D14
constexpr int GSM_TX_PIN = 13; // ESP32 D13 -> SIM800L RXD
constexpr uint32_t GSM_BAUD = 9600;

constexpr int OLED_SDA_PIN = 21;
constexpr int OLED_SCK_PIN = 22;
constexpr uint8_t OLED_ADDRESS = 0x3C;

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t REPORT_INTERVAL_MS = 2000;
constexpr uint32_t MAX_FIX_AGE_MS = 5000;

HardwareSerial GNSS(2);
HardwareSerial GSM(1);
TinyGPSPlus gps;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

bool oledReady = false;
uint32_t lastReport = 0;

void updateOLED(bool hasFreshFix) {
  if (!oledReady) {
    return;
  }

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);

  oled.setTextSize(1);
  oled.setCursor(0, 4);
  oled.print(hasFreshFix ? "GPS FIX" : "GPS STATUS");

  if (hasFreshFix) {
    oled.setCursor(0, 16);
    oled.print("LAT");

    oled.setTextSize(2);
    oled.setCursor(0, 24);
    oled.print(gps.location.lat(), 5);

    oled.setTextSize(1);
    oled.setCursor(0, 40);
    oled.print("LON");

    oled.setTextSize(2);
    oled.setCursor(0, 48);
    oled.print(gps.location.lng(), 5);
  } else {
    oled.setTextSize(2);
    oled.setCursor(0, 20);
    oled.print("Waiting");
    oled.setCursor(0, 40);
    oled.print("for fix");
  }

  oled.display();
}

void printGPSReport(bool hasFreshFix) {
  Serial.println();
  Serial.println("========== GPS REPORT ==========");

  if (hasFreshFix) {
    Serial.println("GPS FIX: AVAILABLE");

    Serial.print("Latitude: ");
    Serial.println(gps.location.lat(), 6);

    Serial.print("Longitude: ");
    Serial.println(gps.location.lng(), 6);

    Serial.print("Satellites: ");
    if (gps.satellites.isValid() && gps.satellites.age() < MAX_FIX_AGE_MS) {
      Serial.println(gps.satellites.value());
    } else {
      Serial.println("Unknown");
    }

    Serial.print("Altitude: ");
    if (gps.altitude.isValid() && gps.altitude.age() < MAX_FIX_AGE_MS) {
      Serial.print(gps.altitude.meters(), 2);
      Serial.println(" m");
    } else {
      Serial.println("Unknown");
    }

    Serial.print("Speed: ");
    if (gps.speed.isValid() && gps.speed.age() < MAX_FIX_AGE_MS) {
      Serial.print(gps.speed.kmph(), 2);
      Serial.println(" km/h");
    } else {
      Serial.println("Unknown");
    }

    Serial.print("HDOP: ");
    if (gps.hdop.isValid() && gps.hdop.age() < MAX_FIX_AGE_MS) {
      Serial.println(gps.hdop.hdop(), 2);
    } else {
      Serial.println("Unknown");
    }
  } else {
    Serial.println("GPS FIX: NOT AVAILABLE");
  }

  Serial.print("GPS characters received: ");
  Serial.println(gps.charsProcessed());
  Serial.println("================================");
}

struct Position {
  double latitude, longitude, speed;
  uint32_t capturedAt, epoch;
  bool gpsFix;
};
QueueHandle_t uploadQueue;
constexpr char TRACKER_HOST[] = "tracker-production-87a1f764.up.railway.app";
constexpr uint32_t UPLOAD_INTERVAL_MS = 15000;

void uploadTask(void *) {
  TinyGsm modem(GSM);
  TinyGsmClient transport(modem);
  ESP_SSLClient https;
  https.setClient(&transport);
  https.setCACert(TRACKER_CA);
  https.setTimeout(15000);
  Position point;
  bool initialized = false;
  for (;;) {
    if (xQueueReceive(uploadQueue, &point, portMAX_DELAY) != pdTRUE) continue;
    if (millis() - point.capturedAt > 60000) continue;
    if (!initialized) {
      if (!modem.init()) {
        Serial.println("[UPLOAD] Modem unavailable; retrying in 15 seconds.");
        vTaskDelay(pdMS_TO_TICKS(15000)); continue;
      }
      if (strlen(TRACKER_SIM_PIN)) modem.simUnlock(TRACKER_SIM_PIN);
      initialized = true;
    }
    if (!modem.isNetworkConnected()) {
      if (!modem.waitForNetwork(60000L)) {
        Serial.println("[UPLOAD] Waiting for cellular network."); continue;
      }
    }
    if (!modem.isGprsConnected() &&
        !modem.gprsConnect(TRACKER_APN, TRACKER_APN_USER, TRACKER_APN_PASSWORD)) {
      Serial.println("[UPLOAD] GPRS failed; check APN and SIM data balance."); continue;
    }
    // Network setup can take time: take the newest queued GPS fix.
    Position newer;
    if (xQueueReceive(uploadQueue, &newer, 0) == pdTRUE) point = newer;
    if (millis() - point.capturedAt > 60000) continue;
    uint32_t epoch = point.epoch;
    if (!epoch) {
      int y, mo, d, h, mi, sec; float offset;
      if (!modem.getNetworkTime(&y, &mo, &d, &h, &mi, &sec, &offset) ||
          y < 2025 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31) {
        Serial.println("[UPLOAD] Waiting for network or GPS clock for verified TLS.");
        continue;
      }
      struct tm utc = {};
      utc.tm_year=y-1900; utc.tm_mon=mo-1; utc.tm_mday=d;
      utc.tm_hour=h; utc.tm_min=mi; utc.tm_sec=sec;
      epoch = static_cast<uint32_t>(mktime(&utc) - offset * 3600);
    } else epoch += (millis() - point.capturedAt) / 1000;
    https.setX509Time(epoch);
    if (!https.connect(TRACKER_HOST, 443)) {
      Serial.println("[UPLOAD] Verified TLS connection failed.");
      https.stop(); continue;
    }
    char body[256];
    snprintf(body, sizeof(body),
      "{\"device_id\":\"%s\",\"latitude\":%.6f,\"longitude\":%.6f,\"speed\":%.2f}",
      TRACKER_DEVICE_ID, point.latitude, point.longitude, point.speed);
    if (!point.gpsFix) {
      snprintf(body, sizeof(body),
        "{\"device_id\":\"%s\",\"type\":\"heartbeat\",\"gps_fix\":false,\"uptime_ms\":%lu}",
        TRACKER_DEVICE_ID, static_cast<unsigned long>(millis()));
    }
    https.print("POST /api/tracker HTTP/1.1\r\nHost: ");
    https.print(TRACKER_HOST);
    https.print("\r\nContent-Type: application/json\r\nX-API-Key: ");
    https.print(TRACKER_API_KEY);
    https.print("\r\nConnection: close\r\nContent-Length: ");
    https.print(strlen(body)); https.print("\r\n\r\n"); https.print(body);
    String status = https.readStringUntil('\n');
    Serial.println(status.startsWith("HTTP/1.1 201") || status.startsWith("HTTP/1.0 201")
      ? (point.gpsFix ? "[UPLOAD] Position saved to dashboard." : "[UPLOAD] Heartbeat saved; waiting for GPS fix.")
      : "[UPLOAD] Not acknowledged: " + status);
    https.stop();
  }
}

void queuePosition() {
  static uint32_t lastUpload = 0;
  if (millis() - lastUpload < UPLOAD_INTERVAL_MS) return;
  const bool fix = gps.location.isValid() && gps.location.age() < MAX_FIX_AGE_MS;
  const bool clock = gps.date.isValid() && gps.time.isValid() &&
    gps.date.age() < MAX_FIX_AGE_MS && gps.time.age() < MAX_FIX_AGE_MS;
  struct tm utc = {};
  utc.tm_year = gps.date.year() - 1900; utc.tm_mon = gps.date.month() - 1;
  utc.tm_mday = gps.date.day(); utc.tm_hour = gps.time.hour();
  utc.tm_min = gps.time.minute(); utc.tm_sec = gps.time.second();
  Position point = {gps.location.lat(), gps.location.lng(),
    gps.speed.isValid() && gps.speed.age() < MAX_FIX_AGE_MS ? gps.speed.kmph() : 0,
    millis(), clock ? static_cast<uint32_t>(mktime(&utc)) : 0, fix};
  xQueueOverwrite(uploadQueue, &point);
  lastUpload = millis();
}

void setup() {
  Serial.begin(SERIAL_BAUD);

  // The pin arguments are ESP32 RX first, then ESP32 TX.
  GNSS.setRxBufferSize(2048);
  GNSS.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  GSM.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);

  Wire.begin(OLED_SDA_PIN, OLED_SCK_PIN);
  Wire.beginTransmission(OLED_ADDRESS);
  oledReady = Wire.endTransmission() == 0 &&
              oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false);

  if (!oledReady) {
    Serial.println("OLED setup failed. Check wiring and I2C address.");
  }

  setenv("TZ", "UTC0", 1);
  tzset();
  uploadQueue = xQueueCreate(1, sizeof(Position));
  if (!uploadQueue || xTaskCreate(uploadTask, "tracker-upload", 16384, nullptr, 1, nullptr) != pdPASS) {
    Serial.println("Upload task allocation failed.");
    while (true) delay(1000);
  }
  updateOLED(false);
  Serial.println(
      "GPS tracker ready. Heartbeats start with cellular connection and valid network or GPS clock.");
}

void loop() {
  while (GNSS.available() > 0) {
    gps.encode(static_cast<char>(GNSS.read()));
  }

  queuePosition();

  const uint32_t now = millis();
  if (now - lastReport >= REPORT_INTERVAL_MS) {
    lastReport = now;

    const bool hasFreshFix =
        gps.location.isValid() && gps.location.age() < MAX_FIX_AGE_MS;

    updateOLED(hasFreshFix);
    printGPSReport(hasFreshFix);
  }

  delay(1);
}