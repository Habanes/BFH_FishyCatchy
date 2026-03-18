#include "web_config.hpp"

#include <string.h>

#include "cJSON.h"
#include "config_store.hpp"
#include "esp_http_server.h"
#include "esp_log.h"

namespace {

constexpr const char* TAG = "WebConfig";
httpd_handle_t g_server = nullptr;
AppContext* g_app_context = nullptr;
ConfigSavedCallback g_saved_callback = nullptr;

const char* kIndexHtml = R"html(
<!doctype html>
<html lang=\"en\">
<head>
<meta charset=\"utf-8\" />
<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />
<title>Fishy Catchy Config</title>
<style>
:root {
  --bg: #f4f2ea;
  --panel: #fffaf0;
  --ink: #102a43;
  --accent: #0f766e;
  --accent-soft: #dff7f3;
  --border: #d9e2ec;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  font-family: "Trebuchet MS", "Gill Sans", sans-serif;
  background: radial-gradient(circle at top right, #fff, var(--bg));
  color: var(--ink);
}
main {
  max-width: 920px;
  margin: 24px auto;
  padding: 18px;
}
.card {
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 18px;
  padding: 20px;
  box-shadow: 0 12px 35px rgba(16, 42, 67, 0.08);
}
h1 { margin-top: 0; }
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 14px;
}
label { display: block; font-size: 0.9rem; margin-bottom: 6px; }
input, select {
  width: 100%;
  border: 1px solid var(--border);
  border-radius: 10px;
  padding: 10px;
  font-size: 0.95rem;
  background: #fff;
}
.actions { margin-top: 18px; display: flex; gap: 10px; align-items: center; }
button {
  background: var(--accent);
  color: #fff;
  border: 0;
  border-radius: 999px;
  padding: 10px 18px;
  font-weight: 700;
  cursor: pointer;
}
button:disabled { opacity: 0.6; cursor: not-allowed; }
#status { font-size: 0.95rem; font-weight: 700; }
.hint { margin-top: 8px; color: #486581; font-size: 0.85rem; }
</style>
</head>
<body>
<main>
  <div class=\"card\">
    <h1>Fishy Catchy</h1>
    <p>Set your detection and device behavior, then save. Saving immediately turns WiFi off.</p>
    <div class=\"grid\">
      <div><label>WiFi Name</label><input id=\"wifi_ssid\" maxlength=\"32\" /></div>
      <div><label>WiFi Shutdown Delay (s)</label><input id=\"wifi_shutdown_delay_sec\" type=\"number\" min=\"5\" max=\"900\" /></div>
      <div><label>Sensor Poll (ms)</label><input id=\"sensor_poll_ms\" type=\"number\" min=\"10\" max=\"2000\" /></div>
      <div><label>Algorithm</label>
        <select id=\"algorithm\">
          <option value=\"single\">Cartesian delta single threshold</option>
          <option value=\"density\">Cartesian delta density threshold</option>
          <option value=\"cumulative\">Cumulative magnitude threshold</option>
        </select>
      </div>
      <div><label>Bite Threshold</label><input id=\"bite_threshold\" type=\"number\" /></div>
      <div><label>Density Window Samples</label><input id=\"density_window_samples\" type=\"number\" min=\"2\" max=\"120\" /></div>
      <div><label>Density Required Hits</label><input id=\"density_threshold_hits\" type=\"number\" min=\"1\" max=\"120\" /></div>
      <div><label>Cumulative Threshold</label><input id=\"cumulative_threshold\" type=\"number\" /></div>
      <div><label>LED Brightness (%)</label><input id=\"led_brightness\" type=\"number\" min=\"0\" max=\"100\" /></div>
      <div><label>LED Pattern Idle</label><input id=\"led_pattern_idle\" type=\"number\" min=\"0\" max=\"9\" /></div>
      <div><label>LED Pattern Connected</label><input id=\"led_pattern_connected\" type=\"number\" min=\"0\" max=\"9\" /></div>
      <div><label>LED Pattern Caught</label><input id=\"led_pattern_caught\" type=\"number\" min=\"0\" max=\"9\" /></div>
    </div>
    <div class=\"actions\">
      <button id=\"saveBtn\">Save And Shutdown WiFi</button>
      <div id=\"status\"></div>
    </div>
    <div class=\"hint\">Tip: after Save, reconnect power to re-open WiFi setup mode.</div>
  </div>
</main>
<script>
const fields = [
  'wifi_ssid', 'wifi_shutdown_delay_sec', 'sensor_poll_ms', 'algorithm', 'bite_threshold',
  'density_window_samples', 'density_threshold_hits', 'cumulative_threshold',
  'led_brightness', 'led_pattern_idle', 'led_pattern_connected', 'led_pattern_caught'
];

function setStatus(msg, ok=true) {
  const el = document.getElementById('status');
  el.textContent = msg;
  el.style.color = ok ? '#0f766e' : '#b42318';
}

async function loadConfig() {
  const r = await fetch('/config');
  const cfg = await r.json();
  for (const key of fields) {
    const el = document.getElementById(key);
    if (el && cfg[key] !== undefined) el.value = cfg[key];
  }
}

async function saveConfig() {
  const btn = document.getElementById('saveBtn');
  btn.disabled = true;
  setStatus('Saving...');
  const payload = {};
  for (const key of fields) {
    const el = document.getElementById(key);
    if (!el) continue;
    if (el.type === 'number') payload[key] = Number(el.value);
    else payload[key] = el.value;
  }
  const r = await fetch('/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  if (r.ok) {
    setStatus('Saved. WiFi is shutting down now.');
  } else {
    const t = await r.text();
    setStatus('Save failed: ' + t, false);
    btn.disabled = false;
  }
}

document.getElementById('saveBtn').addEventListener('click', saveConfig);
loadConfig().catch(() => setStatus('Could not load current config', false));
</script>
</body>
</html>
)html";

const char* algorithm_to_string(DetectionAlgorithm alg) {
    switch (alg) {
        case DetectionAlgorithm::SingleThreshold:
            return "single";
        case DetectionAlgorithm::DensityThreshold:
            return "density";
        case DetectionAlgorithm::CumulativeThreshold:
            return "cumulative";
        default:
            return "single";
    }
}

DetectionAlgorithm string_to_algorithm(const char* value) {
    if (strcmp(value, "density") == 0) {
        return DetectionAlgorithm::DensityThreshold;
    }
    if (strcmp(value, "cumulative") == 0) {
        return DetectionAlgorithm::CumulativeThreshold;
    }
    return DetectionAlgorithm::SingleThreshold;
}

esp_err_t root_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t config_get_handler(httpd_req_t* req) {
    Config config{};
    if (ConfigStore::load(config) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config load failed");
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid", config.wifi_ssid);
    cJSON_AddNumberToObject(root, "wifi_shutdown_delay_sec", config.wifi_shutdown_delay_sec);
    cJSON_AddNumberToObject(root, "sensor_poll_ms", config.sensor_poll_ms);
    cJSON_AddStringToObject(root, "algorithm", algorithm_to_string(config.algorithm));
    cJSON_AddNumberToObject(root, "bite_threshold", config.bite_threshold);
    cJSON_AddNumberToObject(root, "density_window_samples", config.density_window_samples);
    cJSON_AddNumberToObject(root, "density_threshold_hits", config.density_threshold_hits);
    cJSON_AddNumberToObject(root, "cumulative_threshold", config.cumulative_threshold);
    cJSON_AddNumberToObject(root, "led_brightness", config.led_brightness);
    cJSON_AddNumberToObject(root, "led_pattern_idle", config.led_pattern_idle);
    cJSON_AddNumberToObject(root, "led_pattern_connected", config.led_pattern_connected);
    cJSON_AddNumberToObject(root, "led_pattern_caught", config.led_pattern_caught);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json build failed");
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    cJSON_free(json);
    return err;
}

esp_err_t save_post_handler(httpd_req_t* req) {
    if (req->content_len <= 0 || req->content_len > 1024) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body length");
    }

    char body[1025];
    memset(body, 0, sizeof(body));

    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body read failed");
    }
    body[received] = '\0';

    cJSON* root = cJSON_Parse(body);
    if (root == nullptr) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    Config config{};
    esp_err_t err = ConfigStore::load(config);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config read failed");
    }

    cJSON* n;

    n = cJSON_GetObjectItemCaseSensitive(root, "wifi_ssid");
    if (cJSON_IsString(n) && n->valuestring != nullptr) {
        strncpy(config.wifi_ssid, n->valuestring, sizeof(config.wifi_ssid) - 1);
        config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
    }

    n = cJSON_GetObjectItemCaseSensitive(root, "wifi_shutdown_delay_sec");
    if (cJSON_IsNumber(n)) config.wifi_shutdown_delay_sec = static_cast<uint16_t>(n->valuedouble);

    n = cJSON_GetObjectItemCaseSensitive(root, "sensor_poll_ms");
    if (cJSON_IsNumber(n)) config.sensor_poll_ms = static_cast<uint16_t>(n->valuedouble);

    n = cJSON_GetObjectItemCaseSensitive(root, "algorithm");
    if (cJSON_IsString(n) && n->valuestring != nullptr) {
        config.algorithm = string_to_algorithm(n->valuestring);
    }

    n = cJSON_GetObjectItemCaseSensitive(root, "bite_threshold");
    if (cJSON_IsNumber(n)) config.bite_threshold = static_cast<uint32_t>(n->valuedouble);

    n = cJSON_GetObjectItemCaseSensitive(root, "density_window_samples");
    if (cJSON_IsNumber(n)) config.density_window_samples = static_cast<uint16_t>(n->valuedouble);

    n = cJSON_GetObjectItemCaseSensitive(root, "density_threshold_hits");
    if (cJSON_IsNumber(n)) config.density_threshold_hits = static_cast<uint16_t>(n->valuedouble);

    n = cJSON_GetObjectItemCaseSensitive(root, "cumulative_threshold");
    if (cJSON_IsNumber(n)) config.cumulative_threshold = static_cast<uint32_t>(n->valuedouble);

    n = cJSON_GetObjectItemCaseSensitive(root, "led_brightness");
    if (cJSON_IsNumber(n)) config.led_brightness = static_cast<uint8_t>(n->valuedouble);

    n = cJSON_GetObjectItemCaseSensitive(root, "led_pattern_idle");
    if (cJSON_IsNumber(n)) config.led_pattern_idle = static_cast<uint8_t>(n->valuedouble);

    n = cJSON_GetObjectItemCaseSensitive(root, "led_pattern_connected");
    if (cJSON_IsNumber(n)) config.led_pattern_connected = static_cast<uint8_t>(n->valuedouble);

    n = cJSON_GetObjectItemCaseSensitive(root, "led_pattern_caught");
    if (cJSON_IsNumber(n)) config.led_pattern_caught = static_cast<uint8_t>(n->valuedouble);

    cJSON_Delete(root);

    err = ConfigStore::save(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Config save failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid config");
    }

    if (g_saved_callback != nullptr) {
        g_saved_callback();
    }

    xEventGroupSetBits(g_app_context->events, EVENT_WIFI_STOP);
    return httpd_resp_sendstr(req, "ok");
}

}  // namespace

esp_err_t web_config_start(AppContext* app_context, ConfigSavedCallback callback) {
    if (g_server != nullptr) {
        return ESP_OK;
    }

    g_app_context = app_context;
    g_saved_callback = callback;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 3;
    config.stack_size = 6144;

    esp_err_t err = httpd_start(&g_server, &config);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &root_uri);

    httpd_uri_t get_config_uri = {
        .uri = "/config",
        .method = HTTP_GET,
        .handler = config_get_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &get_config_uri);

    httpd_uri_t save_uri = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(g_server, &save_uri);

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}

void web_config_stop() {
    if (g_server != nullptr) {
        httpd_stop(g_server);
        g_server = nullptr;
        ESP_LOGI(TAG, "Web server stopped");
    }
}
