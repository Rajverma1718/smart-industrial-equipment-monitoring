# Smart Industrial Equipment Monitoring & Predictive Maintenance

Simulator-ready ESP32 project for Wokwi.

## What it demonstrates

- DHT22 temperature and humidity sensing over a GPIO data line
- MPU6050 vibration and temperature sensing over I2C
- SSD1306 OLED dashboard over I2C
- microSD telemetry logging over SPI
- GPIO alarm outputs: green/yellow/red LEDs and buzzer
- UART telemetry in the Serial Monitor
- A potentiometer used as a safe simulated motor-current input
- A push button for dashboard-page selection
- A lightweight condition score and predictive-maintenance estimate

## How to run

1. Open the Wokwi project.
2. Start the simulation.
3. Open the Serial Monitor at 115200 baud.
4. Click the DHT22 and adjust temperature/humidity.
5. Click the MPU6050 and increase acceleration or rotation to simulate vibration.
6. Turn the potentiometer to simulate motor current.
7. Press the blue button to change the OLED page.

The firmware calculates a health score from temperature, vibration, current, and humidity. It logs CSV data to `/maintenance.csv` on the simulated SD card and raises a maintenance alert when the score falls below the configured threshold.

> This is a realistic embedded-system prototype and simulator model. It is not a safety-certified industrial controller and must not be connected directly to live machinery without engineering review and appropriate protection.
