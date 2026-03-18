#include "system_state.hpp"

#include <string.h>

bool SystemState_Init(SharedSystemState* store) {
  if (store == nullptr) {
    return false;
  }

  store->mutex = xSemaphoreCreateMutex();
  if (store->mutex == nullptr) {
    return false;
  }

  memset(&store->state, 0, sizeof(SystemState));
  return true;
}

bool SystemState_GetCopy(SharedSystemState* store, SystemState* out_state) {
  if (store == nullptr || out_state == nullptr) {
    return false;
  }

  if (xSemaphoreTake(store->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  *out_state = store->state;
  xSemaphoreGive(store->mutex);
  return true;
}

bool SystemState_SetWifi(SharedSystemState* store, bool enabled, bool client_active) {
  if (store == nullptr) {
    return false;
  }

  if (xSemaphoreTake(store->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  store->state.wifi_enabled = enabled;
  store->state.wifi_client_active = client_active;

  xSemaphoreGive(store->mutex);
  return true;
}

bool SystemState_MarkWebActivity(SharedSystemState* store, uint32_t tick_ms) {
  if (store == nullptr) {
    return false;
  }

  if (xSemaphoreTake(store->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  store->state.wifi_client_active = true;
  store->state.last_web_activity_tick_ms = tick_ms;

  xSemaphoreGive(store->mutex);
  return true;
}

bool SystemState_RegisterCatch(SharedSystemState* store, uint32_t tick_ms) {
  if (store == nullptr) {
    return false;
  }

  if (xSemaphoreTake(store->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  store->state.fish_caught_latched = true;
  store->state.last_catch_tick_ms = tick_ms;
  store->state.fish_catch_count += 1;

  xSemaphoreGive(store->mutex);
  return true;
}

bool SystemState_ClearCatchLatch(SharedSystemState* store) {
  if (store == nullptr) {
    return false;
  }

  if (xSemaphoreTake(store->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  store->state.fish_caught_latched = false;

  xSemaphoreGive(store->mutex);
  return true;
}

bool SystemState_SetConfigVersionApplied(SharedSystemState* store,
                                         uint32_t sensor_version,
                                         uint32_t processor_version,
                                         uint32_t led_version) {
  if (store == nullptr) {
    return false;
  }

  if (xSemaphoreTake(store->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  if (sensor_version != 0) {
    store->state.config_version_applied_sensor = sensor_version;
  }
  if (processor_version != 0) {
    store->state.config_version_applied_processor = processor_version;
  }
  if (led_version != 0) {
    store->state.config_version_applied_led = led_version;
  }

  xSemaphoreGive(store->mutex);
  return true;
}
