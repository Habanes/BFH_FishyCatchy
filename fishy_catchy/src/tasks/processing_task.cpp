#include "tasks/processing_task.hpp"

#include "core/app_context.hpp"
#include "core/sensor_data.hpp"
#include "core/system_config.hpp"
#include "core/system_state.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

uint32_t squared_magnitude(const fishy_catchy::core::SensorSample& sample) {
	const int64_t x = static_cast<int64_t>(sample.x);
	const int64_t y = static_cast<int64_t>(sample.y);
	const int64_t z = static_cast<int64_t>(sample.z);
	return static_cast<uint32_t>((x * x) + (y * y) + (z * z));
}

}  // namespace

namespace fishy_catchy::tasks {

void processing_task(void* task_arg) {
	auto* params = static_cast<ProcessingTaskParams*>(task_arg);
	if (params == nullptr || params->app_context == nullptr) {
		vTaskDelete(nullptr);
		return;
	}

	auto& context = *params->app_context;
	auto& queue = context.sensor_queue();
	auto& store = context.persistent_store();
	auto& state = context.system_state();

	core::SystemConfig config{};
	if (!store.load(&config)) {
		config = core::make_default_system_config();
	}
	config = core::sanitize_system_config(config);

	std::vector<uint32_t> jerk_ring(config.analysis_window_size, 0u);
	std::size_t ring_write_index = 0;
	std::size_t valid_samples = 0;
	bool has_previous_magnitude = false;
	uint32_t previous_magnitude = 0;
	TickType_t last_catch_tick = 0;
	uint32_t processed_samples = 0;

	while (!context.stop_requested()) {
		core::SensorSample sample{};
		if (!queue.pop(&sample, pdMS_TO_TICKS(250))) {
			continue;
		}

		const uint32_t magnitude = squared_magnitude(sample);
		if (!has_previous_magnitude) {
			previous_magnitude = magnitude;
			has_previous_magnitude = true;
			continue;
		}

		const uint32_t delta = (magnitude >= previous_magnitude)
								   ? (magnitude - previous_magnitude)
								   : (previous_magnitude - magnitude);
		previous_magnitude = magnitude;

		if (!jerk_ring.empty()) {
			jerk_ring[ring_write_index] = delta;
			ring_write_index = (ring_write_index + 1) % jerk_ring.size();
			valid_samples = std::min(valid_samples + 1, jerk_ring.size());
		}

		std::size_t over_threshold_count = 0;
		for (std::size_t i = 0; i < valid_samples; ++i) {
			if (jerk_ring[i] > config.bite_threshold) {
				++over_threshold_count;
			}
		}

		if (over_threshold_count >= config.density_threshold) {
			state.set_fish_caught_status(core::FishCaughtStatus::Yes);
			last_catch_tick = xTaskGetTickCount();
		}

		// Auto-clear stale catch indications after a short hold window.
		if (state.get_fish_caught_status() == core::FishCaughtStatus::Yes) {
			const TickType_t elapsed = xTaskGetTickCount() - last_catch_tick;
			if (elapsed > pdMS_TO_TICKS(5000)) {
				state.set_fish_caught_status(core::FishCaughtStatus::No);
			}
		}

		++processed_samples;
		if ((processed_samples % 64u) == 0u) {
			if (store.load(&config)) {
				config = core::sanitize_system_config(config);
			}
			if (config.analysis_window_size != jerk_ring.size()) {
				jerk_ring.assign(config.analysis_window_size, 0u);
				ring_write_index = 0;
				valid_samples = 0;
			}
		}
	}

	vTaskDelete(nullptr);
}

}  // namespace fishy_catchy::tasks
