#pragma once

#include <stdbool.h>

#include "app_types.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct SharedSystemState {
  SystemState state;
  SemaphoreHandle_t mutex;
};

bool SystemState_Init(SharedSystemState* store);
bool SystemState_GetCopy(SharedSystemState* store, SystemState* out_state);
bool SystemState_SetWifi(SharedSystemState* store, bool enabled, bool client_active);
bool SystemState_MarkWebActivity(SharedSystemState* store, uint32_t tick_ms);
bool SystemState_RegisterCatch(SharedSystemState* store, uint32_t tick_ms);
bool SystemState_ClearCatchLatch(SharedSystemState* store);
bool SystemState_SetConfigVersionApplied(SharedSystemState* store,
                                         uint32_t sensor_version,
                                         uint32_t processor_version,
                                         uint32_t led_version);
