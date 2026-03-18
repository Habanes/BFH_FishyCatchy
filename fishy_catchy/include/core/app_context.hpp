#pragma once

#include "core/persistent_store.hpp"
#include "core/sensor_data.hpp"
#include "core/system_state.hpp"

#include <atomic>

namespace fishy_catchy::core {

class AppContext {
public:
	AppContext() = default;

	bool init();
	bool is_initialized() const;

	SystemState& system_state();
	PersistentStore& persistent_store();
	SensorSampleQueue& sensor_queue();

	bool stop_requested() const;
	void request_stop();

private:
	bool initialized_ = false;
	std::atomic<bool> stop_requested_ = false;

	SystemState system_state_{};
	PersistentStore persistent_store_{};
	SensorSampleQueue sensor_queue_{};
};

}  // namespace fishy_catchy::core
