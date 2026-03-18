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

* **Persistent Config Storage:** Non-volatile memory storing constants (e.g., sensor polling rate, bite thresholds).
    * *Access Control:* Read/Write access by the WiFi Task. Read-only access by Application Tasks (loaded at startup).
    * *Concurrency:* Protected by a FreeRTOS Mutex.
* **System State:** Volatile shared memory containing real-time system status (e.g., connection status, catch flags).
    * *Access Control:* Read/Write access shared across multiple processes. 
    * *Concurrency:* Protected by a FreeRTOS Mutex.
* **Sensor Data Queue:** A fixed-size FreeRTOS Queue acting as a thread-safe pipe between the Sensor Task and Processing Task. Passes structs containing raw X, Y, and Z axis data.

## Fish Strike Detection Methodology

The system utilizes a decoupled Producer-Consumer architecture to analyze physical movement. Raw tri-axial data is maintained in a local Circular Buffer (Ring Buffer) by the Processing Task, allowing for flexible algorithmic analysis of a sliding time window. 

**Primary Algorithm (Sustained Jerk Detection):**
1. **Calculate Delta:** Compute the squared magnitude `(X*X) + (Y*Y) + (Z*Z)` for the newest sample and calculate the absolute difference against the previous sample's squared magnitude.
2. **Evaluate Window:** Iterate through the historical samples in the circular buffer. Count how many samples exceed the configured `Bite_Threshold`.
3. **Trigger:** If the count exceeds the `Density_Threshold` (e.g., X high-jerk events within the last Y samples), a catch is registered.

## System Architecture & FreeRTOS Tasks

### Core 0: Communications (WiFi Core)
* **WiFi Task:**
    * Initializes a WiFi AP on boot.
    * Runs a timer on startup. Disables the WiFi radio if no client connects within the configured timeframe.
    * Reads/Writes to the System State to relay real-time telemetry.
    * Reads/Writes to Persistent Config to apply user settings.

### Core 1: Application (Real-Time Core)
* **Sensor Task (Producer):**
    * Wakes up at a strict periodic interval defined by the Persistent Config.
    * Polls the MC6470 IMU over I2C for raw X, Y, and Z values.
    * Pushes the tri-axial data struct into the FreeRTOS Sensor Data Queue.
* **Processing Task (Consumer):**
    * Blocks while waiting for new data in the Sensor Data Queue.
    * Upon receiving data, pushes the X, Y, and Z values into its local Circular Buffer.
    * Executes the detection algorithm over the current sliding window.
    * If a catch is detected, acquires the System State Mutex, updates the catch flag, and releases the Mutex.
* **LED Task:**
    * Periodically acquires the System State Mutex to read the current state.
    * Drives the 7 LEDs to display specific visual patterns based on system status (idle, active connection, fish caught).