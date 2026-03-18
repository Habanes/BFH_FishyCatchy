# Fishy Catchy (ESP32 + FreeRTOS)

Simple, low-power oriented reference implementation of the Fishy Catchy architecture:

- Core application tasks (sensor, processing, LED)
- SoftAP + single-page web configuration
- Persistent config in NVS
- Runtime config reload signaling
- Optional low-power WiFi shutdown behavior

## Build

Prerequisites: ESP-IDF v5.x installed and exported.

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Current hardware integration status

- IMU driver is currently a lightweight placeholder in `main/sensor_driver.cpp`.
- LED driver is currently a lightweight placeholder in `main/led_controller.cpp`.

Replace those modules with real MC6470 + NeoPixel implementations while keeping task and config architecture unchanged.
