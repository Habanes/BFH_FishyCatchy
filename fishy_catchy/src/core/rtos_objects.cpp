#include "core/rtos_objects.hpp"

namespace fishy_catchy::core {

bool create_task_on_core(
	TaskFunction_t task_fn,
	const char* task_name,
	uint32_t stack_depth_words,
	void* task_arg,
	UBaseType_t priority,
	BaseType_t core_id,
	TaskHandle_t* out_handle) {
	if (task_fn == nullptr || task_name == nullptr || out_handle == nullptr || stack_depth_words == 0) {
		return false;
	}

#if defined(ESP_PLATFORM)
	return xTaskCreatePinnedToCore(
			   task_fn,
			   task_name,
			   stack_depth_words,
			   task_arg,
			   priority,
			   out_handle,
			   core_id) == pdPASS;
#else
	(void)core_id;
	return xTaskCreate(task_fn, task_name, stack_depth_words, task_arg, priority, out_handle) == pdPASS;
#endif
}

void delete_task(TaskHandle_t* task_handle) {
	if (task_handle == nullptr || *task_handle == nullptr) {
		return;
	}

	vTaskDelete(*task_handle);
	*task_handle = nullptr;
}

}  // namespace fishy_catchy::core
