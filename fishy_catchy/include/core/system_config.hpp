#pragma once

#include <cstddef>
#include <cstdint>

namespace fishy_catchy::core {

struct SystemConfig {
	uint32_t sensor_poll_interval_ms = 20;
	uint32_t bite_threshold = 800000;
	uint32_t density_threshold = 5;
	std::size_t analysis_window_size = 12;
	std::size_t sensor_queue_capacity = 32;
	uint32_t wifi_ap_timeout_ms = 120000;
	uint32_t led_update_period_ms = 100;
};

SystemConfig make_default_system_config();
SystemConfig sanitize_system_config(const SystemConfig& config);

}  // namespace fishy_catchy::core
