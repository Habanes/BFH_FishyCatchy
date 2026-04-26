Fishycatchy
RTOS and C++ Project Work

Authors: Hannes Stalder, Rafael Pieren
Version and date: Version 1.0, 18.03.2026
Bern University of Applied Sciences
Engineering and Computer Science
Electrical Engineering and Information Technology


1 Summary

Many fishermen and women face the same issue: during long waiting periods, their attention drifts, causing them to miss the exact moment when a fish bites. This problem becomes even more significant at night, where bite detection is typically limited to simple mechanical indicators such as small bells, which are often unreliable or easy to overlook.
This project presents a modern solution to these challenges in the form of an electronic buoy acting as an intelligent bite indicator. The device detects fishing activity in real time and provides clear visual feedback through integrated lighting, ensuring reliable operation even in low-light conditions. In addition, the system can be configured via a web application, allowing users to adapt its behavior to their individual preferences and fishing environments.


2 System Context

The Fishy Catchy device is a self-contained embedded system built around a custom ESP32 Pico v3 board. The system has two physical inputs and two physical outputs.

Inputs:
- IMU (Inertial Measurement Unit): A 3-axis accelerometer connected via I2C (addresses 0x4C or 0x6C, 400 kHz). It continuously measures acceleration caused by a fish pulling the line. This is the primary detection sensor.
- WiFi (IEEE 802.11): The ESP32 opens a software access point on boot. A phone or laptop connects to configure the device and observe live sensor data.

Outputs:
- LED strip: A 7-LED WS2812 addressable RGB strip (GPIO 25) provides the main visual feedback. It renders different patterns depending on system state.
- Motor: A DC motor (GPIO 19 wake, GPIO 20 enable, GPIO 4 reverse) provides a mechanical response when a fish bite is confirmed.

The ESP32 runs FreeRTOS with tasks distributed across both cores. Core 0 handles the WiFi access point and HTTP server. Core 1 handles all real-time tasks: sensor sampling, detection processing, and LED rendering. Two mutex-protected shared stores (ConfigStore and SystemStateStore) allow safe communication between cores.


3 Requirements

The following table lists all requirements, their implementation status, and a short test note.

ID    Requirement                                                                                           Status    Test note
R1    The system shall detect a fish bite using IMU acceleration data with a configurable threshold.        done      Verified by shaking the device. Both single-spike and cumulative-average algorithms trigger reliably. Thresholds adjustable via web UI.
R2    The system shall process sensor data at a fixed periodic rate (<=100 ms cycle time).                  done      Sensor task runs with a 2 ms period (500 Hz), confirmed by log timestamps during manual testing.
R3    The system shall indicate a detected fish bite via a visible LED pattern within <=200 ms.             done      LED task polls system state every 40 ms. Chase pattern activates within one frame of a confirmed catch, well under 200 ms.
R4    The system shall operate reliably in low-light and night conditions.                                  done      LED-based feedback is visible in complete darkness. Tested in a darkened room.
R5    The system shall provide at least 3 distinct LED states (idle, WiFi active, fish caught).             partial   Two states are implemented: slow green breath for idle/monitoring, orange chase for fish caught. A separate WiFi-active LED state is not implemented.
R6    The system shall start a WiFi Access Point automatically on boot.                                     done      WifiTask starts unconditionally from app_main. AP with SSID FishyCatchy is visible immediately after power-on.
R7    The system shall disable WiFi automatically if no client connects within a configurable timeout.      done      A 120-second idle timeout is hardcoded (kWifiShutdownDelayMs = 120000). WiFi shuts down automatically when no HTTP activity is detected.
R8    The system shall keep the WiFi AP active as long as a client is connected.                            done      Every HTTP request resets the activity timer via MarkActivity. Verified by holding the config page open for several minutes.
R9    The system shall allow the user to configure system parameters via a web application.                 done      Web UI at 192.168.4.1 exposes algorithm, thresholds, window time, cooldown, and LED brightness. Settings are saved with a single button press.
R10   The system shall store user settings in non-volatile memory.                                          done      ConfigStore persists the AppConfig struct as a binary blob in the NVS partition. Settings survive power cycles.
R11   The system shall apply updated configuration only after a system reboot.                              partial   Config is written to NVS and read back on next boot. However, all running tasks also hot-reload config when the version number changes, so changes take effect immediately without a reboot.
R12   The system shall provide real-time telemetry data via WiFi.                                          done      The /sensor HTTP endpoint returns a JSON object with current X/Y/Z acceleration, abs-sum, cumulative average, detected flag, and algorithm. The web UI polls this every 300 ms.
R13   The system shall ensure thread-safe access to shared system state using FreeRTOS mutex.               done      Both SharedConfigStore and SharedSystemState contain a FreeRTOS semaphore. All read and write functions take and release the mutex.
R14   The system shall run communication and real-time tasks on separate cores.                             done      WifiTask is pinned to Core 0. SensorTask, ProcessorTask, and LedTask are all pinned to Core 1.
R15   The system shall handle concurrent read/write access to shared memory without data corruption.        done      All access to shared state goes through the mutex-protected ConfigStore_GetCopy, ConfigStore_UpdateAndPersist, SystemState_GetCopy, and the SystemState_Update family of functions.
R16   The system shall operate continuously for at least 10 minutes without memory leaks or crashes.        done      Device ran for over 30 minutes during integration testing with no crashes or observable memory growth.
R17   The system shall monitor stack and heap usage and remain within allocated limits.                      open      No explicit runtime heap or stack monitoring is implemented in the firmware.
R18   The system shall consume minimal power by disabling unused components.                                done      WiFi shuts down automatically after 120 seconds of inactivity. The motor is only driven when a catch is detected. LEDs run at configurable brightness.


4 Design

4.1 Data Flow Diagram

The DFD describes how data moves through the system at three levels of abstraction.

Level 0 (context diagram): The Fishy Catchy system has two external entities: the IMU sensor providing raw acceleration data, and the user interacting via WiFi. The system processes sensor input and produces LED output and a WiFi configuration/telemetry interface.

Level 1 (core split): The system is split across two processor cores. Core 1 owns the real-time path: it reads from the IMU, runs detection, and drives the LED. Core 0 owns the WiFi path: it serves the HTTP configuration UI and the live sensor endpoint. The two cores exchange data through two shared memory regions: SystemState (written by Core 1, read by Core 0 for telemetry) and Config (written by Core 0 via the web UI save, read by Core 1 for detection parameters). Both regions are mutex-protected to prevent data races.

Level 2 (task detail): Within Core 1, SensorTask reads the accelerometer at 500 Hz over I2C and pushes SensorSample structs into a FreeRTOS queue (depth 48). ProcessorTask blocks on this queue, applies the configured detection algorithm, and writes results to SystemStateStore. LedTask reads SystemStateStore at 25 fps and renders the appropriate LED pattern. On Core 0, WifiTask serves HTTP requests, reads ConfigStore for the /config endpoint, reads SystemStateStore for the /sensor endpoint, and writes back to ConfigStore when the user saves settings.

4.2 Class Structure Diagram

[diagram]

The diagram shows five active participants: the entry point (MainApp), two shared data stores (ConfigStore, SystemStateStore), a FreeRTOS queue (SensorQueue), and four FreeRTOS tasks. MainApp initializes the stores and starts all four tasks. The data flow follows a pipeline: SensorTask produces samples into the queue, ProcessorTask consumes them and writes detection results to SystemStateStore. LedTask reads SystemStateStore to decide which LED pattern to show. WifiTask is the only component with write access to ConfigStore, representing the user-facing configuration path. All other tasks only read from ConfigStore.

4.3 Detection State Machine

[diagram]

After startup the system transitions from IDLE to MONITORING as soon as the sensor task delivers the first valid sample. In MONITORING, every incoming SensorSample is evaluated against the configured threshold. Two algorithms are available: Single Spike checks whether the current absolute sum of the three acceleration axes exceeds a threshold in one sample. Cumulative Average computes a running average over a configurable time window and compares that average to a threshold. If the threshold is not exceeded the system continues collecting. When a detection fires, the system registers the catch timestamp in SystemStateStore and enters a cooldown period (default 2500 ms, configurable). During cooldown, new detections are suppressed to prevent a single event from triggering multiple catches. After cooldown the latch is cleared and the system returns to MONITORING. The LED chase pattern remains active for 5 seconds (kCatchVisualMs) regardless of cooldown duration, giving a clear visual indication even for short cooldown settings.

4.4 Web GUI

The web interface is a single-page HTML application embedded as a string literal inside wifi_task.cpp and served directly from the ESP32 at 192.168.4.1. No external files or network access are required.

The page has two tabs. The Settings tab presents all configurable parameters as form inputs: a dropdown to select the detection algorithm (Single Peak or Cumulative Avg), two range sliders for the respective detection thresholds with live numeric readouts, number inputs for the cumulative window time and catch cooldown duration, and a number input for LED brightness. On load the page fetches current values from the /config endpoint (GET, returns JSON) and populates all fields. Pressing Save and Turn Off WiFi submits the form as a URL-encoded POST to /save. The server validates and writes the new config to NVS, signals a shutdown, and the WiFi access point stops. The browser shows a confirmation message.

The Live Sensor tab shows real-time data without any user interaction. A JavaScript setInterval calls the /sensor endpoint (GET, returns JSON) every 300 ms and updates six displayed values: the three raw acceleration axes in g, a valid flag, the current absolute axis sum, the cumulative average, the current detection status, and which algorithm is active. This tab is primarily useful for calibrating thresholds: the user can observe the noise floor and peak values while physically moving the device to determine appropriate threshold settings.


5 Runtime Statistics

Task                Priority    Stack Allocated    Stack Usage (high water mark)    Average CPU usage
SensorTask          6           6144 B             -                                -
ProcessorTask       7           6144 B             -                                -
LedTask             3           5120 B             -                                -
WifiTask            4           16384 B            -                                -

Time                Heap Usage
~10 s runtime
~1 min runtime
~10 min runtime


6 File Structure

```
main/
  app_types.hpp
  CMakeLists.txt
  idf_component.yml
  main.cpp
  config_store.hpp
  config_store.cpp
  system_state.hpp
  system_state.cpp
  sensor_task.hpp
  sensor_task.cpp
  processor_task.hpp
  processor_task.cpp
  led_task.hpp
  led_task.cpp
  wifi_task.hpp
  wifi_task.cpp
```

app_types.hpp - Shared type definitions: board pin constants, AppConfig struct, SensorSample struct, SystemState struct, enums for DetectionAlgorithm and LedPattern.
CMakeLists.txt - ESP-IDF component build configuration. Registers all source files and sets C++17 standard.
idf_component.yml - Component manifest declaring managed dependencies on espressif/cjson and espressif/led_strip.
main.cpp - Application entry point. Initializes NVS, ConfigStore, SystemStateStore, and the sensor queue, then starts all four FreeRTOS tasks.
config_store.hpp - Interface for the NVS-backed config store: init, get a thread-safe copy, update and persist.
config_store.cpp - Implementation: default values, input sanitization, load from NVS on boot, save to NVS on user update.
system_state.hpp - Interface for the shared runtime state store: all update and read functions.
system_state.cpp - Implementation: mutex-protected state struct with per-subsystem update functions to minimize lock contention.
sensor_task.hpp - Interface for SensorTask: start function.
sensor_task.cpp - Reads the accelerometer at 500 Hz over I2C, pushes SensorSamples into the queue, and drives the motor GPIO on catch.
processor_task.hpp - Interface for ProcessorTask: start function and window size constant.
processor_task.cpp - Consumes samples from the queue, runs single-spike or cumulative-average detection, and registers catch events in SystemStateStore.
led_task.hpp - Interface for LedTask: start function and LED count constant.
led_task.cpp - Renders LED patterns at 25 fps on Core 1. Green breath during idle, orange chase for 5 seconds after a catch.
wifi_task.hpp - Interface for WifiTask: start function.
wifi_task.cpp - Runs the WiFi AP and HTTP server on Core 0. Serves the config UI, /config and /sensor JSON endpoints, and /save. Shuts down after 120 seconds idle or immediately after the user saves settings.


7 User Manual

Prerequisites: The microcontroller has already been flashed with the Fishy Catchy firmware.

Step 1: Power on the device.
Connect a power source to the ESP32 board. The LED strip will start a slow green breathing animation within a few seconds, indicating the system is running and monitoring for bites.

Step 2: Connect to the configuration WiFi.
On your phone or laptop, open the WiFi settings and connect to the network named FishyCatchy. The password is fishycatchy. The network is only available for 2 minutes after power-on if no one connects. If you miss the window, power-cycle the device to restart the access point.

Step 3: Open the configuration page.
Open a web browser and go to 192.168.4.1. The Fishy Catchy configuration page will load. It has two tabs: Settings and Live Sensor.

Step 4: Adjust detection settings.
In the Settings tab you can change the following parameters:
- Algorithm: choose Single Peak to trigger on a single strong jerk, or Cumulative Avg to trigger on sustained vibration over a time window.
- Single Peak (g sum): the acceleration threshold for the Single Peak algorithm. Higher values require a stronger jerk.
- Cumulative Threshold: the average acceleration threshold for the Cumulative Avg algorithm.
- Window Time (ms): how many milliseconds of samples are averaged for the Cumulative Avg algorithm.
- Catch Cooldown (ms): how long after a detected bite before a new detection is possible. Prevents a single event from counting multiple times.
- LED Brightness: brightness of the LED strip from 0 (off) to 255 (maximum).

Use the Live Sensor tab to see real-time accelerometer values and the current detection status. This is useful for setting thresholds: watch the Abs Sum value while moving the fishing rod and set the Single Peak threshold just above the noise level.

Step 5: Save settings.
Press Save and Turn Off WiFi. The settings are written to non-volatile memory and the WiFi access point shuts down. The device is now ready to use.

Step 6: In use.
Attach the device to your fishing rod or line. The green breathing LED indicates the system is monitoring. When a bite is detected, the LEDs switch to a fast orange chase pattern for 5 seconds and the motor activates briefly. After the visual feedback ends, the system automatically returns to monitoring mode.

To change settings again, power-cycle the device and reconnect within 2 minutes.


8 Version Control

Version    Date          Description                               Author
0.1        11.03.2026    First attempts with hardware              Rafael Pieren
0.2        13.03.2026    Hardware tested with independent scripts   Rafael Pieren
1.0        18.03.2026    First working version                     Hannes Stalder
