#include "core/app_context.hpp"

namespace fishy_catchy::core {

bool AppContext::init() {
	if (initialized_) {
		return true;
	}

	if (!persistent_store_.init()) {
		return false;
	}

	SystemConfig config{};
	if (!persistent_store_.load(&config)) {
		return false;
	}
	config = sanitize_system_config(config);

	if (!sensor_queue_.init(config.sensor_queue_capacity)) {
		return false;
	}

	stop_requested_.store(false);
	initialized_ = true;
	return true;
}

bool AppContext::is_initialized() const {
	return initialized_;
}

SystemState& AppContext::system_state() {
	return system_state_;
}

PersistentStore& AppContext::persistent_store() {
	return persistent_store_;
}

SensorSampleQueue& AppContext::sensor_queue() {
	return sensor_queue_;
}

bool AppContext::stop_requested() const {
	return stop_requested_.load();
}

void AppContext::request_stop() {
	stop_requested_.store(true);
}

}  // namespace fishy_catchy::core
