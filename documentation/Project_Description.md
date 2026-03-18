# Fishy Catchy Project Specification

## Overview
The Fishy Catchy software project is a C++ application built on FreeRTOS for a custom dual-core microcontroller board. The system uses an IMU to detect when a fish strikes and visually indicates the event via an LED pattern. It features a temporary WiFi Access Point (AP) for user configuration and real-time telemetry.

## Hardware Specifications
* **Processor:** Dual-Core ESP32 Pico v3
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
    * *Concurrency:* Protected by a FreeRTOS Mutex to prevent read/write collisions during configuration updates.
* **System State:** Volatile shared memory containing real-time system status (e.g., current sensor readings, active flags).
    * *Access Control:* Read/Write access shared across multiple processes on both cores. 
    * *Concurrency:* Protected by a FreeRTOS Mutex to ensure thread-safe operations and prevent race conditions.

## Fish Strike Detection Algorithm

To ensure fast execution and minimal CPU overhead on the microcontroller, the detection algorithm avoids computationally expensive operations like square roots and floating-point math. Instead of calculating the true 3D vector magnitude, the system uses a **Squared Magnitude Delta (Jerk Detection)** approach operating directly on the raw integer values from the IMU.

**The Algorithm Steps:**
1. **Sample:** Read the raw integer acceleration values for the X, Y, and Z axes.
2. **Square and Sum:** Calculate the squared magnitude: `M_sq = (X*X) + (Y*Y) + (Z*Z)`.
3. **Calculate Delta (Jerk):** Compare the current squared magnitude against the previous sample's squared magnitude: `Delta = ABS(M_sq_current - M_sq_previous)`.
4. **Evaluate:** Compare `Delta` against a predefined configurable threshold. 
    * If `Delta > Catch_Threshold`, a fish strike is registered.
    * *Note:* Because the algorithm evaluates the *change* in acceleration between samples rather than absolute acceleration, the static 1G force of gravity is automatically filtered out. This isolates the sudden, high-frequency jerk characteristic of a fish bite while ignoring slow, rolling wave motions.

## System Architecture & FreeRTOS Tasks

### Core 0: Communications (WiFi Core)
* **WiFi Task:**
    * Initializes a WiFi AP on boot.
    * Runs a timer on startup. If no user connects within the set timeframe, the WiFi radio is disabled to conserve power until the next reboot.
    * As long as a client remains connected, the AP stays active.
    * *Operations:* Reads/Writes to the System State to relay real-time info to the connected user and update the system on its network status. Reads/Writes to Persistent Config to allow the user to apply new settings. (Both guarded by their respective Mutexes).

### Core 1: Application (Real-Time Core)
* **Sensor Task:**
    * Periodically polls the MC6470 IMU over I2C.
    * Executes the Squared Magnitude Delta algorithm.
    * Acquires the System State Mutex, writes the resulting event status (catch/no catch), and releases the Mutex.
* **LED Task:**
    * Periodically acquires the System State Mutex to read the current state.
    * Drives the 7 LEDs to display specific visual patterns based on the current state (e.g., idle, active connection, fish caught).