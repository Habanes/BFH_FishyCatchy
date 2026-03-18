#include <atomic>
#include <string.h>

#include "app_types.hpp"
#include "config_store.hpp"
#include "strike_detector.hpp"
#include "web_config.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

constexpr const char* TAG = "FishyCatchy";

AppContext g_app{};
SemaphoreHandle_t g_state_mutex = nullptr;
SystemState g_state{};
std::atomic<uint32_t> g_config_version{1};
int g_client_count = 0;

void state_update(void (*updater)(SystemState&)) {
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        updater(g_state);
        xSemaphoreGive(g_state_mutex);
    }
}

SystemState state_snapshot() {
    SystemState copy{};
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = g_state;
        xSemaphoreGive(g_state_mutex);
    }
    return copy;
}

ImuSample read_imu_placeholder() {
    // Placeholder for MC6470 driver: replace with real I2C read.
    constexpr int16_t base = 1000;
    constexpr int16_t noise = 80;
    const int16_t x = static_cast<int16_t>(base + (esp_random() % (2 * noise)) - noise);
    const int16_t y = static_cast<int16_t>(base + (esp_random() % (2 * noise)) - noise);
    const int16_t z = static_cast<int16_t>(base + (esp_random() % (2 * noise)) - noise);
    return {.x = x, .y = y, .z = z};
}

void apply_led_pattern_placeholder(const Config& config, const SystemState& state) {
    // Placeholder for NeoPixel write logic. Keep this light to save power and CPU time.
    static uint8_t last_pattern = 255;

    uint8_t selected_pattern = config.led_pattern_idle;
    if (state.fish_caught) {
        selected_pattern = config.led_pattern_caught;
    } else if (state.client_connected) {
        selected_pattern = config.led_pattern_connected;
    }

    if (selected_pattern != last_pattern) {
        ESP_LOGI(TAG, "LED pattern=%u brightness=%u%%", selected_pattern, config.led_brightness);
        last_pattern = selected_pattern;
    }
}

void on_config_saved() {
    g_config_version.fetch_add(1, std::memory_order_relaxed);
    ESP_LOGI(TAG, "Config updated from web UI");
}

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        ++g_client_count;
        xEventGroupSetBits(g_app.events, EVENT_CLIENT_CONNECTED);
        state_update([](SystemState& s) { s.client_connected = true; });
        ESP_LOGI(TAG, "Station connected, active=%d", g_client_count);
    }

    if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        g_client_count = (g_client_count > 0) ? (g_client_count - 1) : 0;
        if (g_client_count == 0) {
            xEventGroupSetBits(g_app.events, EVENT_CLIENT_DISCONNECTED);
            state_update([](SystemState& s) { s.client_connected = false; });
        }
        ESP_LOGI(TAG, "Station disconnected, active=%d", g_client_count);
    }

    (void)arg;
    (void)event_data;
}

esp_err_t start_wifi_ap(const Config& config) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));

    wifi_config_t ap_config{};
    strncpy(reinterpret_cast<char*>(ap_config.ap.ssid), config.wifi_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(config.wifi_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 2;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    state_update([](SystemState& s) {
        s.wifi_enabled = true;
        s.client_connected = false;
    });

    ESP_LOGI(TAG, "AP started: SSID=%s", config.wifi_ssid);
    return ESP_OK;
}

void stop_wifi_ap() {
    web_config_stop();
    esp_wifi_stop();
    esp_wifi_deinit();

    state_update([](SystemState& s) {
        s.wifi_enabled = false;
        s.client_connected = false;
    });
    ESP_LOGI(TAG, "WiFi AP stopped");
}

void sensor_task(void* /*param*/) {
    Config cfg{};
    uint32_t local_cfg_version = 0;

    while (true) {
        const uint32_t current_version = g_config_version.load(std::memory_order_relaxed);
        if (current_version != local_cfg_version) {
            if (ConfigStore::load(cfg) == ESP_OK) {
                local_cfg_version = current_version;
            }
        }

        ImuSample sample = read_imu_placeholder();
        (void)xQueueSend(g_app.sensor_queue, &sample, 0);
        vTaskDelay(pdMS_TO_TICKS(cfg.sensor_poll_ms));
    }
}

void processing_task(void* /*param*/) {
    Config cfg{};
    uint32_t local_cfg_version = 0;
    StrikeDetector detector;
    TickType_t catch_until_tick = 0;

    while (true) {
        const uint32_t current_version = g_config_version.load(std::memory_order_relaxed);
        if (current_version != local_cfg_version) {
            if (ConfigStore::load(cfg) == ESP_OK) {
                detector.configure(cfg);
                local_cfg_version = current_version;
                ESP_LOGI(TAG, "Detector reconfigured, algorithm=%d", static_cast<int>(cfg.algorithm));
            }
        }

        ImuSample sample{};
        if (xQueueReceive(g_app.sensor_queue, &sample, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (detector.push_sample(sample)) {
                state_update([](SystemState& s) {
                    s.fish_caught = true;
                    s.catches_total += 1;
                });
                catch_until_tick = xTaskGetTickCount() + pdMS_TO_TICKS(2500);
            }
        }

        if (catch_until_tick != 0 && xTaskGetTickCount() > catch_until_tick) {
            state_update([](SystemState& s) { s.fish_caught = false; });
            catch_until_tick = 0;
        }
    }
}

void led_task(void* /*param*/) {
    Config cfg{};
    uint32_t local_cfg_version = 0;

    while (true) {
        const uint32_t current_version = g_config_version.load(std::memory_order_relaxed);
        if (current_version != local_cfg_version) {
            if (ConfigStore::load(cfg) == ESP_OK) {
                local_cfg_version = current_version;
            }
        }

        const SystemState state = state_snapshot();
        apply_led_pattern_placeholder(cfg, state);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void wifi_task(void* /*param*/) {
    Config cfg{};
    if (ConfigStore::load(cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load config for WiFi task");
        vTaskDelete(nullptr);
        return;
    }

    if (start_wifi_ap(cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi AP");
        vTaskDelete(nullptr);
        return;
    }

    if (web_config_start(&g_app, &on_config_saved) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web config service");
        stop_wifi_ap();
        vTaskDelete(nullptr);
        return;
    }

    const TickType_t start_tick = xTaskGetTickCount();
    bool had_any_client = false;

    while (true) {
        const EventBits_t bits = xEventGroupGetBits(g_app.events);

        if ((bits & EVENT_CLIENT_CONNECTED) != 0) {
            had_any_client = true;
        }

        if ((bits & EVENT_WIFI_STOP) != 0) {
            ESP_LOGI(TAG, "WiFi stop requested via save action");
            break;
        }

        if (!had_any_client) {
            const TickType_t elapsed = xTaskGetTickCount() - start_tick;
            const TickType_t timeout = pdMS_TO_TICKS(cfg.wifi_shutdown_delay_sec * 1000UL);
            if (elapsed >= timeout) {
                ESP_LOGI(TAG, "No client connected within timeout, shutting down WiFi");
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }

    stop_wifi_ap();
    vTaskDelete(nullptr);
}

}  // namespace

extern "C" void app_main(void) {
    ESP_ERROR_CHECK(ConfigStore::init());

    g_state_mutex = xSemaphoreCreateMutex();
    if (g_state_mutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return;
    }

    g_state = {
        .fish_caught = false,
        .wifi_enabled = false,
        .client_connected = false,
        .catches_total = 0,
    };

    g_app.sensor_queue = xQueueCreate(32, sizeof(ImuSample));
    g_app.events = xEventGroupCreate();

    if (g_app.sensor_queue == nullptr || g_app.events == nullptr) {
        ESP_LOGE(TAG, "Failed to create queue/events");
        return;
    }

    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 4096, nullptr, 8, nullptr, 1);
    xTaskCreatePinnedToCore(processing_task, "processing_task", 6144, nullptr, 9, nullptr, 1);
    xTaskCreatePinnedToCore(led_task, "led_task", 4096, nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(wifi_task, "wifi_task", 8192, nullptr, 5, nullptr, 0);

    ESP_LOGI(TAG, "Fishy Catchy started");
}
