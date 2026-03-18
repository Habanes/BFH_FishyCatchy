#pragma once

namespace fishy_catchy::core {
class AppContext;
}

namespace fishy_catchy::tasks {

struct SensorTaskParams {
	core::AppContext* app_context = nullptr;
};

void sensor_task(void* task_arg);

}  // namespace fishy_catchy::tasks
