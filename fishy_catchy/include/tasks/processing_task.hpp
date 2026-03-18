#pragma once

namespace fishy_catchy::core {
class AppContext;
}

namespace fishy_catchy::tasks {

struct ProcessingTaskParams {
	core::AppContext* app_context = nullptr;
};

void processing_task(void* task_arg);

}  // namespace fishy_catchy::tasks
