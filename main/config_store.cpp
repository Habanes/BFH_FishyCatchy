#include "config_store.hpp"

#include <string.h>

#include "esp_log.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {

constexpr const char* TAG = "ConfigStore";
constexpr const char* NVS_NAMESPACE = "fishy_cfg";

SemaphoreHandle_t g_mutex = nullptr;
Config g_cached_config{};
bool g_initialized = false;

const char* algorithm_to_string(DetectionAlgorithm alg) {
    switch (alg) {
        case DetectionAlgorithm::SingleThreshold:
            return "single";
        case DetectionAlgorithm::DensityThreshold:
            return "density";
        case DetectionAlgorithm::CumulativeThreshold:
            return "cumulative";
        default:
            return "single";
    }
}

DetectionAlgorithm string_to_algorithm(const char* value) {
    if (strcmp(value, "density") == 0) {
        return DetectionAlgorithm::DensityThreshold;
    }
    if (strcmp(value, "cumulative") == 0) {
        return DetectionAlgorithm::CumulativeThreshold;
    }
    return DetectionAlgorithm::SingleThreshold;
}

esp_err_t write_all(nvs_handle_t handle, const Config& config) {
    esp_err_t err = nvs_set_u16(handle, "poll_ms", config.sensor_poll_ms);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, "bite_thr", config.bite_threshold);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(handle, "den_win", config.density_window_samples);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(handle, "den_hits", config.density_threshold_hits);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, "cum_thr", config.cumulative_threshold);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "alg", algorithm_to_string(config.algorithm));
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "led_br", config.led_brightness);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "led_idle", config.led_pattern_idle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "led_conn", config.led_pattern_connected);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "led_catch", config.led_pattern_caught);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "ssid", config.wifi_ssid);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(handle, "wifi_dly", config.wifi_shutdown_delay_sec);
    if (err != ESP_OK) return err;
    return nvs_commit(handle);
}

esp_err_t read_all(nvs_handle_t handle, Config& config) {
    Config defaults = ConfigStore::defaults();
    config = defaults;

    (void)nvs_get_u16(handle, "poll_ms", &config.sensor_poll_ms);
    (void)nvs_get_u32(handle, "bite_thr", &config.bite_threshold);
    (void)nvs_get_u16(handle, "den_win", &config.density_window_samples);
    (void)nvs_get_u16(handle, "den_hits", &config.density_threshold_hits);
    (void)nvs_get_u32(handle, "cum_thr", &config.cumulative_threshold);
    (void)nvs_get_u8(handle, "led_br", &config.led_brightness);
    (void)nvs_get_u8(handle, "led_idle", &config.led_pattern_idle);
    (void)nvs_get_u8(handle, "led_conn", &config.led_pattern_connected);
    (void)nvs_get_u8(handle, "led_catch", &config.led_pattern_caught);
    (void)nvs_get_u16(handle, "wifi_dly", &config.wifi_shutdown_delay_sec);

    size_t ssid_size = sizeof(config.wifi_ssid);
    (void)nvs_get_str(handle, "ssid", config.wifi_ssid, &ssid_size);

    char algorithm[16] = "single";
    size_t alg_size = sizeof(algorithm);
    (void)nvs_get_str(handle, "alg", algorithm, &alg_size);
    config.algorithm = string_to_algorithm(algorithm);

    return ESP_OK;
}

}  // namespace

Config ConfigStore::defaults() {
    Config config{};
    config.sensor_poll_ms = 50;
    config.bite_threshold = 120000;
    config.density_window_samples = 20;
    config.density_threshold_hits = 5;
    config.cumulative_threshold = 600000;
    config.algorithm = DetectionAlgorithm::DensityThreshold;
    config.led_brightness = 30;
    config.led_pattern_idle = 1;
    config.led_pattern_connected = 2;
    config.led_pattern_caught = 3;
    strncpy(config.wifi_ssid, "FishyCatchy", sizeof(config.wifi_ssid) - 1);
    config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
    config.wifi_shutdown_delay_sec = 120;
    return config;
}

esp_err_t ConfigStore::validate(const Config& config) {
    if (config.sensor_poll_ms < 10 || config.sensor_poll_ms > 2000) return ESP_ERR_INVALID_ARG;
    if (config.bite_threshold < 100 || config.bite_threshold > 500000000) return ESP_ERR_INVALID_ARG;
    if (config.density_window_samples < 2 || config.density_window_samples > 120) return ESP_ERR_INVALID_ARG;
    if (config.density_threshold_hits < 1 ||
        config.density_threshold_hits > config.density_window_samples) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config.cumulative_threshold < 100 || config.cumulative_threshold > 1000000000) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config.led_brightness > 100) return ESP_ERR_INVALID_ARG;
    if (config.wifi_shutdown_delay_sec < 5 || config.wifi_shutdown_delay_sec > 900) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(config.wifi_ssid) < 1 || strlen(config.wifi_ssid) > 32) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

esp_err_t ConfigStore::init() {
    if (g_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    nvs_handle_t handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    Config loaded{};
    err = read_all(handle, loaded);
    if (err == ESP_OK) {
        err = validate(loaded);
        if (err != ESP_OK) {
            loaded = defaults();
            err = write_all(handle, loaded);
        }
    }

    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    g_cached_config = loaded;
    g_initialized = true;
    ESP_LOGI(TAG, "Config initialized. algorithm=%d", static_cast<int>(g_cached_config.algorithm));
    return ESP_OK;
}

esp_err_t ConfigStore::load(Config& out_config) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    out_config = g_cached_config;
    xSemaphoreGive(g_mutex);
    return ESP_OK;
}

esp_err_t ConfigStore::save(const Config& config) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = validate(config);
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = write_all(handle, config);
        nvs_close(handle);
    }

    if (err == ESP_OK) {
        g_cached_config = config;
    }

    xSemaphoreGive(g_mutex);
    return err;
}
