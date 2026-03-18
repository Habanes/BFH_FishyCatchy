#include "app/main_app.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr uint32_t kWifiTaskStackWords = 4096;
constexpr uint32_t kSensorTaskStackWords = 3072;
constexpr uint32_t kProcessingTaskStackWords = 4096;
constexpr uint32_t kLedTaskStackWords = 3072;

constexpr UBaseType_t kWifiTaskPriority = 3;
constexpr UBaseType_t kSensorTaskPriority = 4;
constexpr UBaseType_t kProcessingTaskPriority = 4;
constexpr UBaseType_t kLedTaskPriority = 2;
}

namespace fishy_catchy::app {

bool MainApp::init() {
	if (!app_context_.init()) {
		app_context_.system_state().set_app_status(core::AppStatus::Error);
		return false;
	}

	app_context_.system_state().set_app_status(core::AppStatus::Startup);
	return true;
}

bool MainApp::start() {
	if (started_) {
		return true;
	}

	auto rollback_startup = [this]() {
		core::delete_task(&task_handles_.led_task);
		core::delete_task(&task_handles_.processing_task);
		core::delete_task(&task_handles_.sensor_task);
		core::delete_task(&task_handles_.wifi_task);
	};

	wifi_task_params_.app_context = &app_context_;
	sensor_task_params_.app_context = &app_context_;
	processing_task_params_.app_context = &app_context_;
	led_task_params_.app_context = &app_context_;

	if (!core::create_task_on_core(
			tasks::wifi_task,
			"wifi_task",
			kWifiTaskStackWords,
			&wifi_task_params_,
			kWifiTaskPriority,
			0,
			&task_handles_.wifi_task)) {
		app_context_.system_state().set_app_status(core::AppStatus::Error);
		return false;
	}

	if (!core::create_task_on_core(
			tasks::sensor_task,
			"sensor_task",
			kSensorTaskStackWords,
			&sensor_task_params_,
			kSensorTaskPriority,
			1,
			&task_handles_.sensor_task)) {
		rollback_startup();
		app_context_.system_state().set_app_status(core::AppStatus::Error);
		return false;
	}

	if (!core::create_task_on_core(
			tasks::processing_task,
			"processing_task",
			kProcessingTaskStackWords,
			&processing_task_params_,
			kProcessingTaskPriority,
			1,
			&task_handles_.processing_task)) {
		rollback_startup();
		app_context_.system_state().set_app_status(core::AppStatus::Error);
		return false;
	}

	if (!core::create_task_on_core(
			tasks::led_task,
			"led_task",
			kLedTaskStackWords,
			&led_task_params_,
			kLedTaskPriority,
			1,
			&task_handles_.led_task)) {
		rollback_startup();
		app_context_.system_state().set_app_status(core::AppStatus::Error);
		return false;
	}

	app_context_.system_state().set_app_status(core::AppStatus::Running);
	started_ = true;
	return true;
}

}  // namespace fishy_catchy::app

extern "C" void app_main(void) {
	static fishy_catchy::app::MainApp app;
	if (!app.init()) {
		return;
	}
	if (!app.start()) {
		return;
	}

	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
