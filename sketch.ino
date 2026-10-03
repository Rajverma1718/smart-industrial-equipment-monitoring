#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHTesp.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// Smart Industrial Energy Monitoring & Predictive Maintenance
// Simulator inputs are isolated transducer simulations. Do not connect to mains.

// ---------------- Pin map ----------------
constexpr uint8_t PIN_DHT = 4;
constexpr uint8_t PIN_CURRENT = 34;       // simulated current transducer, 0-15 A
constexpr uint8_t PIN_VOLTAGE = 35;       // simulated voltage transducer, 0-260 V
constexpr uint8_t PIN_PAGE_BUTTON = 27;
constexpr uint8_t PIN_GREEN_LED = 25;
constexpr uint8_t PIN_YELLOW_LED = 26;
constexpr uint8_t PIN_RED_LED = 33;
constexpr uint8_t PIN_BUZZER = 14;
constexpr uint8_t PIN_SD_CS = 5;

// I2C: SDA 21, SCL 22. SPI: SCK 18, MISO 19, MOSI 23, CS 5.
constexpr uint8_t SCREEN_WIDTH = 128;
constexpr uint8_t SCREEN_HEIGHT = 64;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr float POWER_FACTOR = 0.92f;
constexpr float TARIFF_INR_PER_KWH = 8.50f;

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

DHTesp dht;
Adafruit_MPU6050 mpu;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);

struct Telemetry {
  float temperatureC = 0;
  float humidity = 0;
  float vibrationG = 0;
  float voltageV = 230;
  float currentA = 0;
  float powerW = 0;
  float powerFactor = POWER_FACTOR;
  float energyKWh = 0;
  float costInr = 0;
  float health = 100;
  float maintenanceHours = 720;
  const char* state = "STARTING";
  const char* optimization = "MEASURING";
};

Telemetry telemetry;
uint8_t page = 0;
uint32_t lastSample = 0;
uint32_t lastLog = 0;
uint32_t lastDisplay = 0;
uint32_t lastButton = 0;
uint32_t lastWiFiAttempt = 0;
uint32_t lastEnergyUpdate = 0;
bool sdReady = false;
bool webReady = false;

float clamp01(float value) {
  return constrain(value, 0.0f, 1.0f);
}

void setIndicators() {
  digitalWrite(PIN_GREEN_LED, telemetry.health >= 75 && telemetry.voltageV <= 245);
  digitalWrite(PIN_YELLOW_LED, telemetry.health >= 45 && telemetry.health < 75);
  digitalWrite(PIN_RED_LED, telemetry.health < 45 || telemetry.voltageV > 245 || telemetry.currentA > 12);

  bool alarm = telemetry.health < 45 || telemetry.voltageV > 250 || telemetry.currentA > 13;
  digitalWrite(PIN_BUZZER, alarm && ((millis() / 250) % 2 == 0));
}

void calculateHealth() {
  float tempPenalty = clamp01((telemetry.temperatureC - 35.0f) / 35.0f);
  float vibPenalty = clamp01((telemetry.vibrationG - 0.08f) / 0.65f);
  float currentPenalty = clamp01((telemetry.currentA - 7.0f) / 8.0f);
  float voltagePenalty = clamp01(fabsf(telemetry.voltageV - 230.0f) / 35.0f);
  float humidityPenalty = clamp01((telemetry.humidity - 75.0f) / 25.0f);

  float risk = 0.27f * vibPenalty + 0.24f * tempPenalty +
               0.20f * currentPenalty + 0.19f * voltagePenalty +
               0.10f * humidityPenalty;
  telemetry.health = 100.0f * (1.0f - risk);

  if (telemetry.health >= 75) telemetry.state = "NORMAL";
  else if (telemetry.health >= 45) telemetry.state = "WARNING";
  else telemetry.state = "CRITICAL";

  if (telemetry.currentA > 12 || telemetry.voltageV > 245) {
    telemetry.optimization = "REDUCE LOAD";
  } else if (telemetry.powerW < 250) {
    telemetry.optimization = "STANDBY / LOW LOAD";
  } else {
    telemetry.optimization = "LOAD OPTIMAL";
  }

  telemetry.maintenanceHours = 24.0f + telemetry.health * 7.0f;
}

void updateEnergy(uint32_t now) {
  if (lastEnergyUpdate == 0) {
    lastEnergyUpdate = now;
    return;
  }
  float elapsedHours = (now - lastEnergyUpdate) / 3600000.0f;
  telemetry.energyKWh += (telemetry.powerW / 1000.0f) * elapsedHours;
  telemetry.costInr = telemetry.energyKWh * TARIFF_INR_PER_KWH;
  lastEnergyUpdate = now;
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

  telemetry.currentA = (analogRead(PIN_CURRENT) / 4095.0f) * 15.0f;
  telemetry.voltageV = (analogRead(PIN_VOLTAGE) / 4095.0f) * 260.0f;
  telemetry.powerW = telemetry.voltageV * telemetry.currentA * telemetry.powerFactor;
  calculateHealth();
  setIndicators();
}

void printTelemetry() {
  Serial.printf(
    "TELEMETRY,t=%lu,voltage=%.1fV,current=%.2fA,power=%.1fW,energy=%.5fkWh,cost=Rs%.2f,temp=%.1fC,humidity=%.1f%%,vibration=%.3fg,health=%.1f,state=%s,opt=%s,rul=%.0fh
",
    millis(), telemetry.voltageV, telemetry.currentA, telemetry.powerW,
    telemetry.energyKWh, telemetry.costInr, telemetry.temperatureC,
    telemetry.humidity, telemetry.vibrationG, telemetry.health,
    telemetry.state, telemetry.optimization, telemetry.maintenanceHours);
}

void logTelemetry() {
  if (!sdReady) return;
  File logFile = SD.open("/energy_log.csv", FILE_APPEND);
  if (!logFile) return;
  logFile.printf("%lu,%.2f,%.3f,%.2f,%.5f,%.2f,%.2f,%.2f,%.2f,%.2f,%s,%s,%.0f
",
                 millis(), telemetry.voltageV, telemetry.currentA, telemetry.powerW,
                 telemetry.energyKWh, telemetry.costInr, telemetry.temperatureC,
                 telemetry.humidity, telemetry.vibrationG, telemetry.health,
                 telemetry.state, telemetry.optimization, telemetry.maintenanceHours);
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
  display.setTextSize(1);
  if (page == 0) {
    drawHeader("ENERGY MONITOR");
    display.setCursor(0, 16);
    display.printf("V: %5.1f V  I:%4.1f A
", telemetry.voltageV, telemetry.currentA);
    display.printf("Power: %6.0f W
", telemetry.powerW);
    display.printf("Energy:%7.3f kWh
", telemetry.energyKWh);
    display.printf("Cost: Rs%6.2f
", telemetry.costInr);
    display.printf("PF: %.2f  %s", telemetry.powerFactor, telemetry.optimization);
  } else if (page == 1) {
    drawHeader("ASSET CONDITION");
    display.setCursor(0, 16);
    display.printf("Status: %s
", telemetry.state);
    display.printf("Health: %5.1f %%
", telemetry.health);
    display.printf("Temp:   %5.1f C
", telemetry.temperatureC);
    display.printf("Vib:    %5.3f g
", telemetry.vibrationG);
    display.printf("RUL:  %5.0f h", telemetry.maintenanceHours);
  } else {
    drawHeader("SYSTEM STATUS");
    display.setCursor(0, 16);
    display.printf("WiFi: %s
", webReady ? "ONLINE" : "CONNECTING");
    display.printf("IP: %s
", webReady ? WiFi.localIP().toString().c_str() : "not assigned");
    display.printf("SD: %s
", sdReady ? "LOGGING" : "OFFLINE");
    display.printf("I2C: OLED + MPU6050
");
    display.printf("UART: 115200 baud");
  }
  display.display();
}

String jsonTelemetry() {
  String json = "{";
  json += ""voltage_v":" + String(telemetry.voltageV, 2);
  json += ","current_a":" + String(telemetry.currentA, 3);
  json += ","power_w":" + String(telemetry.powerW, 2);
  json += ","energy_kwh":" + String(telemetry.energyKWh, 5);
  json += ","cost_inr":" + String(telemetry.costInr, 2);
  json += ","temperature_c":" + String(telemetry.temperatureC, 2);
  json += ","humidity_pct":" + String(telemetry.humidity, 2);
  json += ","vibration_g":" + String(telemetry.vibrationG, 4);
  json += ","health_pct":" + String(telemetry.health, 2);
  json += ","state":"" + String(telemetry.state) + """;
  json += ","optimization":"" + String(telemetry.optimization) + """;
  json += ","rul_hours":" + String(telemetry.maintenanceHours, 1);
  json += "}";
  return json;
}

void handleApi() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", jsonTelemetry());
}

void handleDashboard() {
  String html = R"HTML(<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Industrial Energy Monitor</title><style>body{font-family:Arial;background:#0f172a;color:#e2e8f0;margin:20px}h1{color:#38bdf8}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}.card{background:#1e293b;padding:16px;border-radius:12px}.v{font-size:24px;color:#f8fafc}.ok{color:#4ade80}.warn{color:#facc15}.bad{color:#fb7185}</style></head><body><h1>Smart Industrial Energy Monitor</h1><p>ESP32 live telemetry and predictive maintenance</p><div class='grid'><div class='card'>Voltage<div id='v' class='v'>--</div></div><div class='card'>Current<div id='i' class='v'>--</div></div><div class='card'>Power<div id='p' class='v'>--</div></div><div class='card'>Energy<div id='e' class='v'>--</div></div><div class='card'>Estimated Cost<div id='c' class='v'>--</div></div><div class='card'>Asset Health<div id='h' class='v'>--</div></div></div><p id='s'>Loading...</p><script>async function refresh(){let d=await fetch('/api/telemetry').then(r=>r.json());v.textContent=d.voltage_v.toFixed(1)+' V';i.textContent=d.current_a.toFixed(2)+' A';p.textContent=d.power_w.toFixed(0)+' W';e.textContent=d.energy_kwh.toFixed(4)+' kWh';c.textContent='Rs '+d.cost_inr.toFixed(2);h.textContent=d.health_pct.toFixed(1)+'%';s.textContent=d.state+' | '+d.optimization+' | RUL '+d.rul_hours.toFixed(0)+' h';s.className=d.state==='NORMAL'?'ok':(d.state==='WARNING'?'warn':'bad')}setInterval(refresh,1000);refresh();</script></body></html>)HTML";
  server.send(200, "text/html", html);
}

void startWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!webReady) {
      server.on("/", handleDashboard);
      server.on("/api/telemetry", handleApi);
      server.begin();
      webReady = true;
      Serial.print("WIFI: dashboard at http://");
      Serial.println(WiFi.localIP());
    }
    server.handleClient();
    return;
  }
  if (millis() - lastWiFiAttempt > 5000) {
    lastWiFiAttempt = millis();
    Serial.println("WIFI: connecting to Wokwi-GUEST...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);
  }
}

void handleButton() {
  if (digitalRead(PIN_PAGE_BUTTON) == LOW && millis() - lastButton > 300) {
    page = (page + 1) % 3;
    lastButton = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("
SMART INDUSTRIAL ENERGY MONITOR");
  Serial.println("Power, energy, cost and predictive-maintenance node starting...");

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
  }

  SPI.begin(18, 19, 23, PIN_SD_CS);
  sdReady = SD.begin(PIN_SD_CS);
  Serial.println(sdReady ? "SD: ready, energy logging enabled" : "SD: unavailable, logging disabled");
  if (sdReady && !SD.exists("/energy_log.csv")) {
    File header = SD.open("/energy_log.csv", FILE_WRITE);
    if (header) {
      header.println("time_ms,voltage_v,current_a,power_w,energy_kwh,cost_inr,temp_c,humidity_pct,vibration_g,health_pct,state,optimization,rul_hours");
      header.close();
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);
  lastEnergyUpdate = millis();
}

void loop() {
  uint32_t now = millis();
  handleButton();
  startWiFi();

  if (now - lastSample >= 1000) {
    lastSample = now;
    readSensors();
    updateEnergy(now);
    printTelemetry();
  }
  if (now - lastLog >= 5000) {
    lastLog = now;
    logTelemetry();
  }
  if (now - lastDisplay >= 500) {
    lastDisplay = now;
    drawDashboard();
  }
}
