#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <DHTesp.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ---------------- Pin map ----------------
constexpr uint8_t PIN_DHT = 4;
constexpr uint8_t PIN_CURRENT = 34;       // potentiometer = simulated motor current
constexpr uint8_t PIN_PAGE_BUTTON = 27;
constexpr uint8_t PIN_GREEN_LED = 25;
constexpr uint8_t PIN_YELLOW_LED = 26;
constexpr uint8_t PIN_RED_LED = 33;
constexpr uint8_t PIN_BUZZER = 14;

// I2C: SDA 21, SCL 22
// SPI: SCK 18, MISO 19, MOSI 23, CS 5
constexpr uint8_t PIN_SD_CS = 5;

constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;
constexpr uint8_t OLED_ADDRESS = 0x3C;

DHTesp dht;
Adafruit_MPU6050 mpu;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

struct Telemetry {
  float temperatureC = 0;
  float humidity = 0;
  float vibrationG = 0;
  float currentA = 0;
  float health = 100;
  float maintenanceHours = 720;
  const char* state = "STARTING";
};

Telemetry telemetry;
uint8_t page = 0;
uint32_t lastSample = 0;
uint32_t lastLog = 0;
uint32_t lastDisplay = 0;
uint32_t lastButton = 0;
bool sdReady = false;

float clamp01(float value) {
  return constrain(value, 0.0f, 1.0f);
}

void setIndicators() {
  digitalWrite(PIN_GREEN_LED, telemetry.health >= 75);
  digitalWrite(PIN_YELLOW_LED, telemetry.health >= 45 && telemetry.health < 75);
  digitalWrite(PIN_RED_LED, telemetry.health < 45);

  // Audible alarm only for critical state, with a slow pulse.
  bool alarm = telemetry.health < 45 && ((millis() / 250) % 2 == 0);
  digitalWrite(PIN_BUZZER, alarm);
}

void calculateHealth() {
  // Normalized deviation models. Thresholds are intentionally visible for teaching.
  float tempPenalty = clamp01((telemetry.temperatureC - 35.0f) / 35.0f);
  float vibPenalty = clamp01((telemetry.vibrationG - 0.08f) / 0.65f);
  float currentPenalty = clamp01((telemetry.currentA - 7.0f) / 8.0f);
  float humidityPenalty = clamp01((telemetry.humidity - 75.0f) / 25.0f);

  float risk = 0.35f * vibPenalty + 0.30f * tempPenalty +
               0.25f * currentPenalty + 0.10f * humidityPenalty;
  telemetry.health = 100.0f * (1.0f - risk);

  if (telemetry.health >= 75) {
    telemetry.state = "NORMAL";
  } else if (telemetry.health >= 45) {
    telemetry.state = "WARNING";
  } else {
    telemetry.state = "CRITICAL";
  }

  // A simple remaining-useful-life estimate for demonstration purposes.
  // Higher risk shortens the projected service interval.
  telemetry.maintenanceHours = 24.0f + telemetry.health * 7.0f;
}

void readSensors() {
  TempAndHumidity climate = dht.getTempAndHumidity();
  if (!isnan(climate.temperature)) telemetry.temperatureC = climate.temperature;
  if (!isnan(climate.humidity)) telemetry.humidity = climate.humidity;

  sensors_event_t accel, gyro, mpuTemp;
  mpu.getEvent(&accel, &gyro, &mpuTemp);
  float ax = accel.acceleration.x / 9.80665f;
  float ay = accel.acceleration.y / 9.80665f;
  float az = accel.acceleration.z / 9.80665f;
  float magnitude = sqrtf(ax * ax + ay * ay + az * az);
  telemetry.vibrationG = fabsf(magnitude - 1.0f) +
                         (fabsf(gyro.gyro.x) + fabsf(gyro.gyro.y) + fabsf(gyro.gyro.z)) * 0.015f;

  int rawCurrent = analogRead(PIN_CURRENT);
  telemetry.currentA = (rawCurrent / 4095.0f) * 15.0f;
  calculateHealth();
  setIndicators();
}

void printTelemetry() {
  Serial.printf(
    "TELEMETRY,t=%lu,temp=%.1fC,humidity=%.1f%%,vibration=%.3fg,current=%.2fA,health=%.1f,state=%s,rul=%.0fh\n",
    millis(), telemetry.temperatureC, telemetry.humidity, telemetry.vibrationG,
    telemetry.currentA, telemetry.health, telemetry.state, telemetry.maintenanceHours);
}

void logTelemetry() {
  if (!sdReady) return;
  File logFile = SD.open("/maintenance.csv", FILE_APPEND);
  if (!logFile) return;
  logFile.printf("%lu,%.2f,%.2f,%.4f,%.2f,%.2f,%s,%.0f\n",
                 millis(), telemetry.temperatureC, telemetry.humidity,
                 telemetry.vibrationG, telemetry.currentA, telemetry.health,
                 telemetry.state, telemetry.maintenanceHours);
  logFile.close();
}

void drawHeader(const char* title) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(title);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
}

void drawDashboard() {
  display.clearDisplay();
  if (page == 0) {
    drawHeader("MOTOR HEALTH MONITOR");
    display.setCursor(0, 16);
    display.printf("Status: %s\n", telemetry.state);
    display.printf("Health: %5.1f %%\n", telemetry.health);
    display.printf("Temp:   %5.1f C\n", telemetry.temperatureC);
    display.printf("Vib:    %5.3f g\n", telemetry.vibrationG);
    display.printf("Current:%5.2f A", telemetry.currentA);
  } else {
    drawHeader("PREDICTIVE MAINTENANCE");
    display.setCursor(0, 16);
    display.printf("Service in: %.0f h\n", telemetry.maintenanceHours);
    display.printf("Humidity:%5.1f %%\n", telemetry.humidity);
    display.printf("I2C: MPU6050 + OLED\n");
    display.printf("SPI: SD logging %s\n", sdReady ? "ON" : "OFF");
    display.printf("UART: 115200 telemetry");
  }
  display.display();
}

void handleButton() {
  if (digitalRead(PIN_PAGE_BUTTON) == LOW && millis() - lastButton > 300) {
    page = (page + 1) % 2;
    lastButton = millis();
  }
}

void setup() {
  Serial.begin(115200); // UART telemetry and service diagnostics
  delay(200);
  Serial.println("\nSMART INDUSTRIAL EQUIPMENT MONITOR");
  Serial.println("ESP32 condition monitoring prototype starting...");

  pinMode(PIN_GREEN_LED, OUTPUT);
  pinMode(PIN_YELLOW_LED, OUTPUT);
  pinMode(PIN_RED_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_PAGE_BUTTON, INPUT_PULLUP);
  analogReadResolution(12);

  Wire.begin(21, 22);
  dht.setup(PIN_DHT, DHTesp::DHT22);

  if (!mpu.begin()) {
    Serial.println("ERROR: MPU6050 not found");
    while (true) {
      digitalWrite(PIN_RED_LED, !digitalRead(PIN_RED_LED));
      delay(250);
    }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("ERROR: OLED not found");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 20);
    display.println("Industrial Monitor");
    display.println("Starting sensors...");
    display.display();
  }

  SPI.begin(18, 19, 23, PIN_SD_CS);
  sdReady = SD.begin(PIN_SD_CS);
  Serial.println(sdReady ? "SD: ready, logging enabled" : "SD: unavailable, logging disabled");
  if (sdReady && !SD.exists("/maintenance.csv")) {
    File header = SD.open("/maintenance.csv", FILE_WRITE);
    if (header) {
      header.println("time_ms,temp_c,humidity_pct,vibration_g,current_a,health_pct,state,rul_hours");
      header.close();
    }
  }
}

void loop() {
  handleButton();

  if (millis() - lastSample >= 1000) {
    lastSample = millis();
    readSensors();
    printTelemetry();
  }

  if (millis() - lastLog >= 5000) {
    lastLog = millis();
    logTelemetry();
  }

  if (millis() - lastDisplay >= 500) {
    lastDisplay = millis();
    drawDashboard();
  }
}
