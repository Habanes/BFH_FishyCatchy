#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_types.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct SharedConfigStore {
  AppConfig config;
  SemaphoreHandle_t mutex;
  uint32_t version;
};

bool ConfigStore_Init(SharedConfigStore* store);
bool ConfigStore_GetCopy(SharedConfigStore* store, AppConfig* out_config,
                         uint32_t* out_version);
bool ConfigStore_UpdateAndPersist(SharedConfigStore* store,
                                  const AppConfig* new_config);
