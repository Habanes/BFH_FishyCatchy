#include "core/system_config.hpp"

#include <algorithm>

namespace fishy_catchy::core {

SystemConfig make_default_system_config() {
	return SystemConfig{};
}

SystemConfig sanitize_system_config(const SystemConfig& config) {
	SystemConfig sanitized = config;

	sanitized.sensor_poll_interval_ms = std::max<uint32_t>(1, sanitized.sensor_poll_interval_ms);
	sanitized.analysis_window_size = std::max<std::size_t>(1, sanitized.analysis_window_size);
	sanitized.sensor_queue_capacity = std::max<std::size_t>(4, sanitized.sensor_queue_capacity);
	sanitized.wifi_ap_timeout_ms = std::max<uint32_t>(5000, sanitized.wifi_ap_timeout_ms);
	sanitized.led_update_period_ms = std::max<uint32_t>(10, sanitized.led_update_period_ms);

	const uint32_t max_density = static_cast<uint32_t>(sanitized.analysis_window_size);
	sanitized.density_threshold = std::clamp<uint32_t>(sanitized.density_threshold, 1, max_density);

	return sanitized;
}

}  // namespace fishy_catchy::core
