#include "tasks/wifi_task.hpp"

#include "core/app_context.hpp"
#include "core/system_config.hpp"
#include "core/system_state.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr TickType_t kWifiLoopDelayTicks = pdMS_TO_TICKS(200);
constexpr const char* kApSsid = "FishyCatchy";
constexpr const char* kApPass = "";
constexpr uint8_t kApChannel = 1;
constexpr uint8_t kApMaxConnections = 4;

std::atomic<int> g_connected_clients = 0;
std::atomic<bool> g_seen_client = false;

struct HttpContext {
	fishy_catchy::core::AppContext* app_context = nullptr;
};

bool parse_uint_field(const char* payload, const char* key, uint32_t* out_value) {
	if (payload == nullptr || key == nullptr || out_value == nullptr) {
		return false;
	}

	char needle[64] = {0};
	if (std::snprintf(needle, sizeof(needle), "\"%s\"", key) <= 0) {
		return false;
	}

	const char* key_pos = std::strstr(payload, needle);
	if (key_pos == nullptr) {
		return false;
	}

	const char* colon = std::strchr(key_pos, ':');
	if (colon == nullptr) {
		return false;
	}

	char* end_ptr = nullptr;
	const unsigned long value = std::strtoul(colon + 1, &end_ptr, 10);
	if (end_ptr == (colon + 1)) {
		return false;
	}

	*out_value = static_cast<uint32_t>(value);
	return true;
}

esp_err_t telemetry_handler(httpd_req_t* req) {
	auto* context = static_cast<HttpContext*>(req->user_ctx);
	if (context == nullptr || context->app_context == nullptr) {
		return ESP_FAIL;
	}

	auto& app_context = *context->app_context;
	auto& state = app_context.system_state();
	auto& store = app_context.persistent_store();

	fishy_catchy::core::SystemConfig config{};
	if (!store.load(&config)) {
		config = fishy_catchy::core::make_default_system_config();
	}
	config = fishy_catchy::core::sanitize_system_config(config);

	char response[320] = {0};
	(void)std::snprintf(
		response,
		sizeof(response),
		"{\"app_status\":%u,\"wifi_status\":%u,\"fish_caught\":%u,\"clients\":%d,\"sensor_poll_interval_ms\":%u,\"bite_threshold\":%u,\"density_threshold\":%u,\"analysis_window_size\":%u}",
		static_cast<unsigned>(state.get_app_status()),
		static_cast<unsigned>(state.get_wifi_status()),
		static_cast<unsigned>(state.get_fish_caught_status()),
		g_connected_clients.load(),
		static_cast<unsigned>(config.sensor_poll_interval_ms),
		static_cast<unsigned>(config.bite_threshold),
		static_cast<unsigned>(config.density_threshold),
		static_cast<unsigned>(config.analysis_window_size));

	httpd_resp_set_type(req, "application/json");
	return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

esp_err_t config_handler(httpd_req_t* req) {
	auto* context = static_cast<HttpContext*>(req->user_ctx);
	if (context == nullptr || context->app_context == nullptr) {
		return ESP_FAIL;
	}

	if (req->content_len <= 0 || req->content_len > 512) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload length");
		return ESP_FAIL;
	}

	char body[513] = {0};
	int total_read = 0;
	while (total_read < req->content_len) {
		const int received = httpd_req_recv(req, body + total_read, req->content_len - total_read);
		if (received <= 0) {
			httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
			return ESP_FAIL;
		}
		total_read += received;
	}
	body[total_read] = '\0';

	auto& store = context->app_context->persistent_store();
	fishy_catchy::core::SystemConfig config{};
	if (!store.load(&config)) {
		config = fishy_catchy::core::make_default_system_config();
	}

	uint32_t value = 0;
	if (parse_uint_field(body, "sensor_poll_interval_ms", &value)) {
		config.sensor_poll_interval_ms = value;
	}
	if (parse_uint_field(body, "bite_threshold", &value)) {
		config.bite_threshold = value;
	}
	if (parse_uint_field(body, "density_threshold", &value)) {
		config.density_threshold = value;
	}
	if (parse_uint_field(body, "analysis_window_size", &value)) {
		config.analysis_window_size = value;
	}
	if (parse_uint_field(body, "wifi_ap_timeout_ms", &value)) {
		config.wifi_ap_timeout_ms = value;
	}
	if (parse_uint_field(body, "led_update_period_ms", &value)) {
		config.led_update_period_ms = value;
	}

	config = fishy_catchy::core::sanitize_system_config(config);
	if (!store.save(config)) {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
		return ESP_FAIL;
	}

	httpd_resp_set_type(req, "application/json");
	return httpd_resp_sendstr(req, "{\"ok\":true}");
}

void on_wifi_event(void*, esp_event_base_t event_base, int32_t event_id, void*) {
	if (event_base != WIFI_EVENT) {
		return;
	}

	if (event_id == WIFI_EVENT_AP_STACONNECTED) {
		g_seen_client.store(true);
		(void)g_connected_clients.fetch_add(1);
		return;
	}

	if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
		const int previous = g_connected_clients.fetch_sub(1);
		if (previous <= 1) {
			g_connected_clients.store(0);
		}
	}
}
}

namespace fishy_catchy::tasks {

void wifi_task(void* task_arg) {
	auto* params = static_cast<WifiTaskParams*>(task_arg);
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

	g_connected_clients.store(0);
	g_seen_client.store(false);

	if (esp_netif_init() != ESP_OK) {
		state.set_wifi_status(core::WifiStatus::Error);
		vTaskDelete(nullptr);
		return;
	}
	(void)esp_event_loop_create_default();
	esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();
	if (ap_netif == nullptr) {
		state.set_wifi_status(core::WifiStatus::Error);
		vTaskDelete(nullptr);
		return;
	}

	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	if (esp_wifi_init(&wifi_init_config) != ESP_OK) {
		state.set_wifi_status(core::WifiStatus::Error);
		vTaskDelete(nullptr);
		return;
	}

	esp_event_handler_instance_t wifi_event_instance = nullptr;
	if (esp_event_handler_instance_register(
			WIFI_EVENT,
			ESP_EVENT_ANY_ID,
			on_wifi_event,
			nullptr,
			&wifi_event_instance) != ESP_OK) {
		state.set_wifi_status(core::WifiStatus::Error);
		(void)esp_wifi_deinit();
		vTaskDelete(nullptr);
		return;
	}

	wifi_config_t ap_config = {};
	std::strncpy(reinterpret_cast<char*>(ap_config.ap.ssid), kApSsid, sizeof(ap_config.ap.ssid) - 1);
	std::strncpy(reinterpret_cast<char*>(ap_config.ap.password), kApPass, sizeof(ap_config.ap.password) - 1);
	ap_config.ap.ssid_len = static_cast<uint8_t>(std::strlen(kApSsid));
	ap_config.ap.channel = kApChannel;
	ap_config.ap.max_connection = kApMaxConnections;
	ap_config.ap.authmode = std::strlen(kApPass) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

	if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK ||
		esp_wifi_set_config(WIFI_IF_AP, &ap_config) != ESP_OK ||
		esp_wifi_start() != ESP_OK) {
		state.set_wifi_status(core::WifiStatus::Error);
		(void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_instance);
		(void)esp_wifi_deinit();
		vTaskDelete(nullptr);
		return;
	}

	HttpContext http_context{};
	http_context.app_context = &context;

	httpd_handle_t server = nullptr;
	httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
	http_config.max_uri_handlers = 4;
	http_config.stack_size = 6144;

	if (httpd_start(&server, &http_config) == ESP_OK) {
		httpd_uri_t telemetry_uri = {};
		telemetry_uri.uri = "/telemetry";
		telemetry_uri.method = HTTP_GET;
		telemetry_uri.handler = telemetry_handler;
		telemetry_uri.user_ctx = &http_context;

		httpd_uri_t config_uri = {};
		config_uri.uri = "/config";
		config_uri.method = HTTP_POST;
		config_uri.handler = config_handler;
		config_uri.user_ctx = &http_context;
		(void)httpd_register_uri_handler(server, &telemetry_uri);
		(void)httpd_register_uri_handler(server, &config_uri);
	}

	state.set_wifi_status(core::WifiStatus::Searching);

	const TickType_t ap_start_tick = xTaskGetTickCount();
	bool ap_enabled = true;
	uint32_t loop_counter = 0;

	while (!context.stop_requested()) {
		if ((loop_counter % 25u) == 0u) {
			if (store.load(&config)) {
				config = core::sanitize_system_config(config);
			}
		}

		if (ap_enabled) {
			const TickType_t elapsed = xTaskGetTickCount() - ap_start_tick;
			if (!g_seen_client.load() && elapsed >= pdMS_TO_TICKS(config.wifi_ap_timeout_ms)) {
				if (server != nullptr) {
					(void)httpd_stop(server);
					server = nullptr;
				}
				(void)esp_wifi_stop();
				ap_enabled = false;
				state.set_wifi_status(core::WifiStatus::Off);
			} else if (g_connected_clients.load() > 0) {
				state.set_wifi_status(core::WifiStatus::Connected);
			} else {
				state.set_wifi_status(core::WifiStatus::Searching);
			}
		}

		vTaskDelay(kWifiLoopDelayTicks);
		++loop_counter;
	}

	if (server != nullptr) {
		(void)httpd_stop(server);
	}
	if (ap_enabled) {
		(void)esp_wifi_stop();
	}
	(void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_instance);
	(void)esp_wifi_deinit();

	state.set_wifi_status(core::WifiStatus::Off);
	vTaskDelete(nullptr);
}

}  // namespace fishy_catchy::tasks
