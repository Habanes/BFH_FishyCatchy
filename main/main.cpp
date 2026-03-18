#include "config_store.hpp"
#include "led_task.hpp"
#include "processor_task.hpp"
#include "sensor_task.hpp"
#include "system_state.hpp"
#include "wifi_task.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs_flash.h"

namespace {
constexpr char kTag[] = "Main";

SharedConfigStore g_config_store = {};
SharedSystemState g_system_state = {};
QueueHandle_t g_sensor_queue = nullptr;
}  // namespace

extern "C" void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  if (!ConfigStore_Init(&g_config_store)) {
    ESP_LOGE(kTag, "Config store init failed");
    return;
  }

  if (!SystemState_Init(&g_system_state)) {
    ESP_LOGE(kTag, "System state init failed");
    return;
  }

  AppConfig boot_cfg = {};
  uint32_t boot_version = 0;
  if (!ConfigStore_GetCopy(&g_config_store, &boot_cfg, &boot_version)) {
    ESP_LOGE(kTag, "Unable to read boot config");
    return;
  }

  g_sensor_queue = xQueueCreate(boot_cfg.queue_length, sizeof(SensorSample));
  if (g_sensor_queue == nullptr) {
    ESP_LOGE(kTag, "Failed to allocate sensor queue");
    return;
  }

  bool ok_sensor = SensorTask_Start(&g_config_store, &g_system_state, g_sensor_queue);
  bool ok_proc = ProcessorTask_Start(&g_config_store, &g_system_state, g_sensor_queue);
  bool ok_led = LedTask_Start(&g_config_store, &g_system_state);
  bool ok_wifi = WifiTask_Start(&g_config_store, &g_system_state);

  if (!(ok_sensor && ok_proc && ok_led && ok_wifi)) {
    ESP_LOGE(kTag, "Task creation failed (sensor=%d, proc=%d, led=%d, wifi=%d)",
             ok_sensor, ok_proc, ok_led, ok_wifi);
    return;
  }

  ESP_LOGI(kTag, "Fishy Catchy started: all tasks running");
}
