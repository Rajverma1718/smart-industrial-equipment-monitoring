# Industrial Energy Monitoring & Power Optimization System

ESP32-based Wokwi prototype for energy metering, power optimization, IoT telemetry, and predictive maintenance.

## Features

- Simulated voltage transducer: 0–260 V through an analog potentiometer
- Simulated current transducer: 0–15 A through an analog potentiometer
- Real-time power calculation using voltage × current × power factor
- Accumulated energy consumption in kWh
- Estimated electricity cost in INR using a configurable tariff
- DHT22 temperature and humidity condition sensing
- MPU6050 vibration sensing over I2C
- SSD1306 OLED dashboard with energy, condition, and network pages
- microSD CSV logging over SPI to `/energy_log.csv`
- UART telemetry at 115200 baud
- Wi-Fi connection to `Wokwi-GUEST` with a live browser dashboard and JSON API
- Health score, maintenance RUL estimate, alarms, and load-optimization advice

## Run in Wokwi

1. Open the Wokwi project and start the simulation.
2. Open the Serial Monitor at 115200 baud.
3. Turn `CURRENT 0-15A` to simulate motor current.
4. Turn `VOLTAGE 0-260V` to simulate supply voltage.
5. Change the DHT22 temperature/humidity or MPU6050 motion to create maintenance warnings.
6. Press `PAGE` to cycle through Energy, Asset Condition, and System Status screens.
7. The serial output reports the Wi-Fi dashboard URL after connection. Open that URL to see live telemetry.

## Calculations

- `Power (W) = Voltage (V) × Current (A) × Power Factor`
- `Energy (kWh) = Power (W) × elapsed time (hours) / 1000`
- `Cost (INR) = Energy (kWh) × tariff`
- Default power factor: `0.92`
- Default tariff: `₹8.50/kWh`

## Enterprise-style demonstration flow

The project separates sensing, analytics, storage, visualization, and alerts. It provides a compact edge-monitoring pattern that can later be connected to MQTT, InfluxDB, Grafana, or a cloud IoT platform. The health score is a demonstration model, not a certified protection system.

> Safety: this simulator uses potentiometers as isolated sensor substitutes. Never connect an ESP32 directly to mains voltage or current. A physical deployment requires certified voltage/current transducers, isolation, fusing, enclosure design, calibration, and electrical-safety review.
