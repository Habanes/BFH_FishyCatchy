#pragma once

namespace fishy_catchy::core {
class AppContext;
}

namespace fishy_catchy::tasks {

struct WifiTaskParams {
	core::AppContext* app_context = nullptr;
};

void wifi_task(void* task_arg);

}  // namespace fishy_catchy::tasks
