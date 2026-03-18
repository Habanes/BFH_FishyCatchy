# Fishy Catchy (ESP32 + FreeRTOS)

Low-power, dual-core Fishy Catchy implementation with persistent runtime configuration and a one-page AP web UI.

## What this firmware does

- Starts all fish-detection tasks immediately on boot using the last saved config from NVS.
- Starts a temporary WiFi SoftAP and web page for configuration.
- Automatically powers WiFi down after a configurable inactivity timeout.
- If user presses save, writes config to NVS and shuts WiFi task down immediately.
- Running tasks hot-reload config after save using config-version checks.

## Project structure

- `main/main.cpp`: entry point, NVS init, queue creation, task startup.
- `main/wifi_task.hpp` + `main/wifi_task.cpp`: AP + HTTP server + config web UI.
- `main/sensor_task.hpp` + `main/sensor_task.cpp`: MC6470 I2C polling producer task.
- `main/processor_task.hpp` + `main/processor_task.cpp`: detection consumer task.
- `main/led_task.hpp` + `main/led_task.cpp`: animated 7-LED status renderer.
- `main/config_store.hpp` + `main/config_store.cpp`: persistent config store (NVS + mutex).
- `main/system_state.hpp` + `main/system_state.cpp`: shared runtime state (mutex).
- `main/app_types.hpp`: shared structs, pin map, enums, constants.

## Detection algorithms

Selectable in the web UI:

- `0`: Cartesian magnitude spike once.
- `1`: Cartesian magnitude dense spikes over window.
- `2`: Cumulative magnitude over window.

## Build

Prerequisites: ESP-IDF v5.x environment exported.

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Notes

- Shared config and system state access is synchronized via FreeRTOS mutexes (`xSemaphoreTake`/`xSemaphoreGive`).
- Sensor samples are passed through a fixed-size FreeRTOS queue between SensorTask and ProcessorTask.
- WiFi AP SSID/password and all tuning parameters are user-configurable and persisted.
