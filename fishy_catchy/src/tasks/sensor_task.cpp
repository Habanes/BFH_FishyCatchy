#include "tasks/sensor_task.hpp"

#include "core/app_context.hpp"
#include "core/sensor_data.hpp"
#include "core/system_config.hpp"
#include "core/system_state.hpp"

#include <cstdint>

#include <driver/i2c.h>
#include <esp_err.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr i2c_port_t kI2cPort = I2C_NUM_0;
constexpr gpio_num_t kI2cSdaPin = GPIO_NUM_21;
constexpr gpio_num_t kI2cSclPin = GPIO_NUM_22;
constexpr uint8_t kMc6470Address = 0x4C;
constexpr uint8_t kMc6470DataStartRegister = 0x0D;

bool init_i2c_bus() {
	static bool initialized = false;
	if (initialized) {
		return true;
	}

	i2c_config_t i2c_config = {};
	i2c_config.mode = I2C_MODE_MASTER;
	i2c_config.sda_io_num = kI2cSdaPin;
	i2c_config.scl_io_num = kI2cSclPin;
	i2c_config.sda_pullup_en = GPIO_PULLUP_ENABLE;
	i2c_config.scl_pullup_en = GPIO_PULLUP_ENABLE;
	i2c_config.master.clk_speed = 400000;

	if (i2c_param_config(kI2cPort, &i2c_config) != ESP_OK) {
		return false;
	}
	if (i2c_driver_install(kI2cPort, i2c_config.mode, 0, 0, 0) != ESP_OK) {
		return false;
	}

	initialized = true;
	return true;
}

bool read_mc6470_sample(fishy_catchy::core::SensorSample* out_sample) {
	if (out_sample == nullptr) {
		return false;
	}

	uint8_t raw[6] = {0};
	const esp_err_t err = i2c_master_write_read_device(
		kI2cPort,
		kMc6470Address,
		&kMc6470DataStartRegister,
		1,
		raw,
		sizeof(raw),
		pdMS_TO_TICKS(20));
	if (err != ESP_OK) {
		return false;
	}

	out_sample->x = static_cast<int16_t>((static_cast<uint16_t>(raw[1]) << 8U) | raw[0]);
	out_sample->y = static_cast<int16_t>((static_cast<uint16_t>(raw[3]) << 8U) | raw[2]);
	out_sample->z = static_cast<int16_t>((static_cast<uint16_t>(raw[5]) << 8U) | raw[4]);
	return true;
}

}  // namespace

namespace fishy_catchy::tasks {

void sensor_task(void* task_arg) {
	auto* params = static_cast<SensorTaskParams*>(task_arg);
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

	const bool imu_ready = init_i2c_bus();
	if (!imu_ready) {
		state.set_app_status(core::AppStatus::Error);
	}

	TickType_t last_wake = xTaskGetTickCount();
	uint32_t sample_counter = 1;

	while (!context.stop_requested()) {
		const TickType_t period_ticks = pdMS_TO_TICKS(config.sensor_poll_interval_ms);
		vTaskDelayUntil(&last_wake, period_ticks == 0 ? 1 : period_ticks);

		core::SensorSample sample{};
		if (imu_ready && !read_mc6470_sample(&sample)) {
			continue;
		}

		(void)queue.push(sample, pdMS_TO_TICKS(5));

		++sample_counter;
		if ((sample_counter % 64u) == 0u) {
			if (store.load(&config)) {
				config = core::sanitize_system_config(config);
			}
		}
	}

	vTaskDelete(nullptr);
}

}  // namespace fishy_catchy::tasks
