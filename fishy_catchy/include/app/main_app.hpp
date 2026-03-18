#pragma once

#include "core/app_context.hpp"
#include "core/rtos_objects.hpp"
#include "tasks/led_task.hpp"
#include "tasks/processing_task.hpp"
#include "tasks/sensor_task.hpp"
#include "tasks/wifi_task.hpp"

namespace fishy_catchy::app {

class MainApp {
public:
	MainApp() = default;

	bool init();
	bool start();

private:
	core::AppContext app_context_{};
	core::TaskHandles task_handles_{};
	bool started_ = false;

	tasks::WifiTaskParams wifi_task_params_{};
	tasks::SensorTaskParams sensor_task_params_{};
	tasks::ProcessingTaskParams processing_task_params_{};
	tasks::LedTaskParams led_task_params_{};
};

}  // namespace fishy_catchy::app

extern "C" void app_main(void);
