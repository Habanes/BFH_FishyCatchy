#include "tasks/led_task.hpp"

#include "core/app_context.hpp"
#include "core/system_config.hpp"
#include "core/system_state.hpp"

#include <cstdint>

#include <driver/gpio.h>
#include <esp_err.h>
#include <led_strip.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr int kLedCount = 7;
constexpr gpio_num_t kLedDataPin = GPIO_NUM_25;

void set_all(led_strip_handle_t strip, uint8_t r, uint8_t g, uint8_t b) {
	for (int i = 0; i < kLedCount; ++i) {
		(void)led_strip_set_pixel(strip, i, r, g, b);
	}
}

void render_led_pattern(
	led_strip_handle_t strip,
	fishy_catchy::core::AppStatus app_status,
	fishy_catchy::core::WifiStatus wifi_status,
	fishy_catchy::core::FishCaughtStatus fish_status,
	uint32_t frame) {
	if (strip == nullptr) {
		return;
	}

	if (app_status == fishy_catchy::core::AppStatus::Error ||
		wifi_status == fishy_catchy::core::WifiStatus::Error ||
		fish_status == fishy_catchy::core::FishCaughtStatus::Error) {
		set_all(strip, 32, 0, 0);
		(void)led_strip_refresh(strip);
		return;
	}

	if (fish_status == fishy_catchy::core::FishCaughtStatus::Yes) {
		const bool pulse_on = ((frame / 2u) % 2u) == 0u;
		set_all(strip, pulse_on ? 48 : 8, pulse_on ? 30 : 4, 0);
		(void)led_strip_refresh(strip);
		return;
	}

	if (wifi_status == fishy_catchy::core::WifiStatus::Connected) {
		const int lit = static_cast<int>(frame % static_cast<uint32_t>(kLedCount));
		set_all(strip, 0, 0, 0);
		(void)led_strip_set_pixel(strip, lit, 0, 25, 10);
		(void)led_strip_refresh(strip);
		return;
	}

	const bool blink_on = ((frame / 4u) % 2u) == 0u;
	set_all(strip, 0, blink_on ? 8 : 0, 3);
	(void)led_strip_refresh(strip);
}

}  // namespace

namespace fishy_catchy::tasks {

void led_task(void* task_arg) {
	auto* params = static_cast<LedTaskParams*>(task_arg);
	if (params == nullptr || params->app_context == nullptr) {
		vTaskDelete(nullptr);
		return;
	}

	auto& context = *params->app_context;
	auto& store = context.persistent_store();
	auto& state = context.system_state();

	core::SystemConfig config{};
	if (!store.load(&config)) {
		config = core::make_default_system_config();
	}
	config = core::sanitize_system_config(config);

	led_strip_handle_t led_strip = nullptr;
	led_strip_config_t strip_config = {};
	strip_config.strip_gpio_num = kLedDataPin;
	strip_config.max_leds = kLedCount;
	strip_config.led_model = LED_MODEL_WS2812;
	strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
	strip_config.flags.invert_out = false;

	led_strip_rmt_config_t rmt_config = {};
	rmt_config.resolution_hz = 10 * 1000 * 1000;
	rmt_config.flags.with_dma = false;

	if (led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip) != ESP_OK) {
		state.set_app_status(core::AppStatus::Error);
	}

	TickType_t last_wake = xTaskGetTickCount();
	uint32_t frame = 0;

	while (!context.stop_requested()) {
		const TickType_t period_ticks = pdMS_TO_TICKS(config.led_update_period_ms);
		vTaskDelayUntil(&last_wake, period_ticks == 0 ? 1 : period_ticks);

		render_led_pattern(
			led_strip,
			state.get_app_status(),
			state.get_wifi_status(),
			state.get_fish_caught_status(),
			frame);

		++frame;
		if ((frame % 64u) == 0u) {
			if (store.load(&config)) {
				config = core::sanitize_system_config(config);
			}
		}
	}

	if (led_strip != nullptr) {
		(void)led_strip_clear(led_strip);
		(void)led_strip_del(led_strip);
	}

	vTaskDelete(nullptr);
}

}  // namespace fishy_catchy::tasks
