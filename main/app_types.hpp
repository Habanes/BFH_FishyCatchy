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
  kDenseSpikes = 1,
  kCumulative = 2,
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

  char wifi_ssid[33];
  char wifi_password[65];

  uint16_t wifi_shutdown_delay_s;
  uint16_t sensor_period_ms;
  uint16_t queue_length;

  uint8_t led_brightness;
  uint8_t led_idle_pattern;
  uint8_t led_wifi_pattern;
  uint8_t led_catch_pattern;

  uint8_t algorithm;

  float single_spike_threshold;
  float dense_spike_threshold;
  uint16_t dense_window_samples;
  uint16_t dense_required_hits;

  float cumulative_threshold;
  uint16_t cumulative_window_samples;

  uint16_t catch_cooldown_ms;
  uint16_t reserved0;
};

struct SensorSample {
  float ax;
  float ay;
  float az;
  float mx;
  float my;
  float mz;
  bool accel_valid;
  bool mag_valid;
  uint32_t tick_ms;
};

struct SystemState {
  bool wifi_enabled;
  bool wifi_client_active;
  bool fish_caught_latched;

  uint32_t fish_catch_count;
  uint32_t last_catch_tick_ms;
  uint32_t last_web_activity_tick_ms;

  uint32_t config_version_applied_sensor;
  uint32_t config_version_applied_processor;
  uint32_t config_version_applied_led;

  float last_ax;
  float last_ay;
  float last_az;
  float last_mx;
  float last_my;
  float last_mz;
  bool last_accel_valid;
  bool last_mag_valid;
  uint32_t last_sample_tick_ms;

  float calc_abs_axis_sum;
  uint16_t calc_dense_hits;
  float calc_cumulative_sum;
  bool calc_detected;
  uint8_t calc_algorithm;
};

constexpr uint32_t kConfigMagic = 0x46435348;       // FCSH
constexpr uint16_t kConfigSchemaVersion = 2;
constexpr uint16_t kMaxWindowSamples = 256;
constexpr uint16_t kLedCount = 7;
