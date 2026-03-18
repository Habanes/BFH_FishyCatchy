#pragma once

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace fishy_catchy::core {

struct TaskHandles {
	TaskHandle_t wifi_task = nullptr;
	TaskHandle_t sensor_task = nullptr;
	TaskHandle_t processing_task = nullptr;
	TaskHandle_t led_task = nullptr;
};

bool create_task_on_core(
	TaskFunction_t task_fn,
	const char* task_name,
	uint32_t stack_depth_words,
	void* task_arg,
	UBaseType_t priority,
	BaseType_t core_id,
	TaskHandle_t* out_handle);

void delete_task(TaskHandle_t* task_handle);

}  // namespace fishy_catchy::core
