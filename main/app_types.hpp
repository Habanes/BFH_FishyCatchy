#pragma once

#include <stdint.h>

// Board pins for the custom ESP32 Pico v3 design.
namespace BoardPins {
constexpr int kLedData = 25;
constexpr int kMotorWake = 19;
constexpr int kMotorEnable = 20;
constexpr int kMotorReverse = 4;
constexpr int kI2cSda = 21;
constexpr int kI2cScl = 22;
}  // namespace BoardPins

enum class DetectionAlgorithm : uint8_t {
  kSingleSpike = 0,
  kCumulative = 1,
};

enum class LedPattern : uint8_t {
  kSolid = 0,
  kBreath = 1,
  kChase = 2,
  kPulse = 3,
  kRainbow = 4,
};

struct AppConfig {
  uint32_t magic;
  uint16_t schema_version;

  uint8_t led_brightness;
  uint8_t algorithm;

  float single_spike_threshold;
  float cumulative_threshold;
  uint16_t cumulative_window_ms;

  uint16_t catch_cooldown_ms;
};

struct SensorSample {
  float ax;
  float ay;
  float az;
  bool accel_valid;
  uint32_t tick_ms;
};

struct SystemState {
  bool wifi_enabled;

  uint32_t config_version_applied_sensor;
  uint32_t config_version_applied_processor;
  uint32_t config_version_applied_led;

  float last_ax;
  float last_ay;
  float last_az;
  bool last_accel_valid;
  uint32_t last_sample_tick_ms;

  float calc_abs_axis_sum;
  float calc_cumulative_sum;
  bool calc_detected;
  uint8_t calc_algorithm;

  uint32_t last_web_activity_tick_ms;
  uint32_t last_catch_tick_ms;
  bool fish_caught_latched;
};

constexpr uint32_t kConfigMagic = 0x46435348;       // FCSH
constexpr uint16_t kConfigSchemaVersion = 2;
constexpr uint16_t kMaxWindowSamples = 256;
constexpr uint16_t kLedCount = 7;
