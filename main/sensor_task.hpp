#pragma once

#include "config_store.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "system_state.hpp"

bool SensorTask_Start(SharedConfigStore* config_store,
                      SharedSystemState* state_store,
                      QueueHandle_t sample_queue);
