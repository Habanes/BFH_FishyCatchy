# Fishy Catchy Project Specification

## Overview
The Fishy Catchy software project is a C++ application built on FreeRTOS for a custom dual-core microcontroller board. The system uses an IMU to detect when a fish strikes and visually indicates the event via an LED pattern. It features a temporary WiFi Access Point (AP) for user configuration and real-time telemetry.

## Hardware Specifications
* **Processor:** Dual-Core Microcontroller (Labeled STM32 Pico v3)
* **Sensors:** 1x IMU (MC6470)
* **Actuators:** 7x LEDs
* **Pin Mapping:**
    * DAT LED PIN: IO25
    * MOT WAKE: IO19
    * MOT EN: IO20
    * MOT REVERSE: IO4
    * I2C SDA: IO21
    * I2C SCL: IO22

## Data Storage & State Management

* **Persistent Config Storage:** Non-volatile memory storing constants, thresholds, and user settings.
    * *Access Control:* Read/Write access by the WiFi Task. Read-only access by Application Tasks (values are loaded once at startup; changes require a reboot to take effect).
* **System State:** Volatile shared memory containing real-time system status (e.g., current sensor readings, active flags).
    * *Access Control:* Read/Write access shared across multiple processes on both cores. Requires FreeRTOS Mutex protection to ensure thread safety.

## System Architecture & FreeRTOS Tasks

### Core 0: Communications (WiFi Core)
* **WiFi Task:**
    * Initializes a WiFi AP on boot.
    * Runs a timer on startup. If no user connects within the set timeframe, the WiFi radio is disabled to conserve power until the next reboot.
    * As long as a client remains connected, the AP stays active.
    * *Operations:* Reads/Writes to the System State to relay real-time info to the connected user and update the system on its network status. Reads/Writes to Persistent Config to allow the user to apply new settings.

### Core 1: Application (Real-Time Core)
* **Sensor Task:**
    * Periodically polls the MC6470 IMU over I2C.
    * Calculates the magnitude of the acceleration vector.
    * Deducts the standard gravity constant (9.81 m/s²).
    * Compares the normalized magnitude against a predefined catch threshold.
    * Writes the resulting event status (catch/no catch) to the System State.
* **LED Task:**
    * Periodically polls the System State.
    * Drives the 7 LEDs to display specific visual patterns based on the current state (e.g., idle, active connection, fish caught).