#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

enum class DetectionAlgorithm : uint8_t {
    SingleThreshold = 0,
    DensityThreshold = 1,
    CumulativeThreshold = 2,
};

struct Config {
    uint16_t sensor_poll_ms;
    uint32_t bite_threshold;
    uint16_t density_window_samples;
    uint16_t density_threshold_hits;
    uint32_t cumulative_threshold;
    DetectionAlgorithm algorithm;
    uint8_t led_brightness;
    uint8_t led_pattern_idle;
    uint8_t led_pattern_connected;
    uint8_t led_pattern_caught;
    char wifi_ssid[33];
    uint16_t wifi_shutdown_delay_sec;
};

struct SystemState {
    bool fish_caught;
    bool wifi_enabled;
    bool client_connected;
    uint32_t catches_total;
};

struct ImuSample {
    int16_t x;
    int16_t y;
    int16_t z;
};

constexpr EventBits_t EVENT_CONFIG_UPDATED = BIT0;
constexpr EventBits_t EVENT_WIFI_STOP = BIT1;
constexpr EventBits_t EVENT_CLIENT_CONNECTED = BIT2;
constexpr EventBits_t EVENT_CLIENT_DISCONNECTED = BIT3;

struct AppContext {
    QueueHandle_t sensor_queue;
    EventGroupHandle_t events;
};
