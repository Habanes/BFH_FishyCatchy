#include "config_store.hpp"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr char kTag[] = "ConfigStore";
constexpr char kNvsNamespace[] = "fishy";
constexpr char kNvsKeyConfig[] = "config_blob";

void SetDefaults(AppConfig* cfg) {
  memset(cfg, 0, sizeof(AppConfig));
  cfg->magic = kConfigMagic;
  cfg->schema_version = kConfigSchemaVersion;

  strncpy(cfg->wifi_ssid, "FishyCatchy", sizeof(cfg->wifi_ssid) - 1);
  strncpy(cfg->wifi_password, "fishycatchy", sizeof(cfg->wifi_password) - 1);

  cfg->wifi_shutdown_delay_s = 120;
  cfg->sensor_period_ms = 2;
  cfg->queue_length = 48;

  cfg->led_brightness = 96;
  cfg->led_idle_pattern = static_cast<uint8_t>(LedPattern::kBreath);
  cfg->led_wifi_pattern = static_cast<uint8_t>(LedPattern::kChase);
  cfg->led_catch_pattern = static_cast<uint8_t>(LedPattern::kPulse);

  cfg->algorithm = static_cast<uint8_t>(DetectionAlgorithm::kDenseSpikes);

  cfg->single_spike_threshold = 1.80f;
  cfg->dense_spike_threshold = 1.20f;
  cfg->dense_window_samples = 20;
  cfg->dense_required_hits = 4;

  cfg->cumulative_threshold = 18.0f;
  cfg->cumulative_window_samples = 20;

  cfg->catch_cooldown_ms = 2500;
}

void SanitizeConfig(AppConfig* cfg) {
  if (cfg->magic != kConfigMagic || cfg->schema_version != kConfigSchemaVersion) {
    SetDefaults(cfg);
    return;
  }

  cfg->wifi_ssid[sizeof(cfg->wifi_ssid) - 1] = '\0';
  cfg->wifi_password[sizeof(cfg->wifi_password) - 1] = '\0';

  if (cfg->wifi_shutdown_delay_s < 10 || cfg->wifi_shutdown_delay_s > 1800) {
    cfg->wifi_shutdown_delay_s = 120;
  }
  if (cfg->sensor_period_ms < 1 || cfg->sensor_period_ms > 1000) {
    cfg->sensor_period_ms = 2;
  }
  if (cfg->queue_length < 8 || cfg->queue_length > 128) {
    cfg->queue_length = 48;
  }

  if (cfg->led_idle_pattern > static_cast<uint8_t>(LedPattern::kRainbow)) {
    cfg->led_idle_pattern = static_cast<uint8_t>(LedPattern::kBreath);
  }
  if (cfg->led_wifi_pattern > static_cast<uint8_t>(LedPattern::kRainbow)) {
    cfg->led_wifi_pattern = static_cast<uint8_t>(LedPattern::kChase);
  }
  if (cfg->led_catch_pattern > static_cast<uint8_t>(LedPattern::kRainbow)) {
    cfg->led_catch_pattern = static_cast<uint8_t>(LedPattern::kPulse);
  }

  if (cfg->algorithm > static_cast<uint8_t>(DetectionAlgorithm::kCumulative)) {
    cfg->algorithm = static_cast<uint8_t>(DetectionAlgorithm::kDenseSpikes);
  }

  if (cfg->single_spike_threshold < 0.05f || cfg->single_spike_threshold > 50.0f) {
    cfg->single_spike_threshold = 1.80f;
  }
  if (cfg->dense_spike_threshold < 0.05f || cfg->dense_spike_threshold > 50.0f) {
    cfg->dense_spike_threshold = 1.20f;
  }
  if (cfg->dense_window_samples < 4 || cfg->dense_window_samples > kMaxWindowSamples) {
    cfg->dense_window_samples = 20;
  }
  if (cfg->dense_required_hits < 1 || cfg->dense_required_hits > cfg->dense_window_samples) {
    cfg->dense_required_hits = 4;
  }

  if (cfg->cumulative_threshold < 0.10f || cfg->cumulative_threshold > 500.0f) {
    cfg->cumulative_threshold = 18.0f;
  }
  if (cfg->cumulative_window_samples < 4 || cfg->cumulative_window_samples > kMaxWindowSamples) {
    cfg->cumulative_window_samples = 20;
  }

  if (cfg->catch_cooldown_ms < 100 || cfg->catch_cooldown_ms > 20000) {
    cfg->catch_cooldown_ms = 2500;
  }

  cfg->magic = kConfigMagic;
  cfg->schema_version = kConfigSchemaVersion;
}

bool SaveToNvs(const AppConfig* cfg) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs_open failed: %s", esp_err_to_name(err));
    return false;
  }

  err = nvs_set_blob(handle, kNvsKeyConfig, cfg, sizeof(AppConfig));
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);

  if (err != ESP_OK) {
    ESP_LOGE(kTag, "SaveToNvs failed: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

bool LoadFromNvs(AppConfig* cfg) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "nvs_open on load failed: %s", esp_err_to_name(err));
    return false;
  }

  size_t required_size = sizeof(AppConfig);
  err = nvs_get_blob(handle, kNvsKeyConfig, cfg, &required_size);
  nvs_close(handle);

  if (err != ESP_OK || required_size != sizeof(AppConfig)) {
    return false;
  }

  return true;
}
}  // namespace

bool ConfigStore_Init(SharedConfigStore* store) {
  if (store == nullptr) {
    return false;
  }

  store->mutex = xSemaphoreCreateMutex();
  if (store->mutex == nullptr) {
    return false;
  }

  SetDefaults(&store->config);
  AppConfig loaded = {};
  if (LoadFromNvs(&loaded)) {
    SanitizeConfig(&loaded);
    store->config = loaded;
  } else {
    SaveToNvs(&store->config);
  }

  store->version = 1;
  ESP_LOGI(kTag, "Config initialized (version=%lu)", static_cast<unsigned long>(store->version));
  return true;
}

bool ConfigStore_GetCopy(SharedConfigStore* store, AppConfig* out_config,
                         uint32_t* out_version) {
  if (store == nullptr || out_config == nullptr) {
    return false;
  }

  if (xSemaphoreTake(store->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  *out_config = store->config;
  if (out_version != nullptr) {
    *out_version = store->version;
  }

  xSemaphoreGive(store->mutex);
  return true;
}

bool ConfigStore_UpdateAndPersist(SharedConfigStore* store,
                                  const AppConfig* new_config) {
  if (store == nullptr || new_config == nullptr) {
    return false;
  }

  AppConfig sanitized = *new_config;
  SanitizeConfig(&sanitized);

  if (!SaveToNvs(&sanitized)) {
    return false;
  }

  if (xSemaphoreTake(store->mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
    return false;
  }

  store->config = sanitized;
  store->version += 1;
  uint32_t new_version = store->version;

  xSemaphoreGive(store->mutex);

  ESP_LOGI(kTag, "Config updated and persisted (version=%lu)",
           static_cast<unsigned long>(new_version));
  return true;
}
