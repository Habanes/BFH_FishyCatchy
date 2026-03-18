#pragma once

#include "app_types.hpp"

#include "esp_err.h"

typedef void (*ConfigSavedCallback)();

esp_err_t web_config_start(AppContext* app_context, ConfigSavedCallback callback);
void web_config_stop();
