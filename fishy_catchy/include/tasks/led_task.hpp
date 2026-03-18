#pragma once

namespace fishy_catchy::core {
class AppContext;
}

namespace fishy_catchy::tasks {

struct LedTaskParams {
	core::AppContext* app_context = nullptr;
};

void led_task(void* task_arg);

}  // namespace fishy_catchy::tasks
