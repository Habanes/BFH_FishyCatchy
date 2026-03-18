#include "wifi_task.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

namespace {
constexpr char kTag[] = "WifiTask";

struct WifiTaskContext {
  SharedConfigStore* config_store;
  SharedSystemState* state_store;

  esp_netif_t* ap_netif;
  httpd_handle_t server;

  volatile bool shutdown_requested;
  volatile uint32_t startup_tick_ms;
  volatile uint32_t last_activity_tick_ms;
};

const char* kIndexHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width,initial-scale=1" />
<title>Fishy Catchy Config</title>
<style>
:root {
  --bg1: #f7f8f2;
  --bg2: #e7f1ec;
  --card: #ffffff;
  --ink: #17352a;
  --muted: #5a7067;
  --accent: #007a5a;
  --accent-2: #0fa36f;
  --line: #d9e6df;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  font-family: "Avenir Next", "Nunito Sans", "Segoe UI", sans-serif;
  color: var(--ink);
  background:
    radial-gradient(1200px 800px at 10% -10%, #d8ebe2 0%, transparent 60%),
    radial-gradient(900px 700px at 110% 10%, #d9efe7 0%, transparent 50%),
    linear-gradient(160deg, var(--bg1), var(--bg2));
  min-height: 100vh;
}
.wrap {
  max-width: 860px;
  margin: 0 auto;
  padding: 28px 16px 48px;
}
.card {
  background: var(--card);
  border: 1px solid var(--line);
  border-radius: 18px;
  box-shadow: 0 10px 30px rgba(20, 58, 43, 0.08);
  overflow: hidden;
}
.header {
  padding: 20px 22px;
  background: linear-gradient(120deg, #fefefe, #eef6f1);
  border-bottom: 1px solid var(--line);
}
.header h1 {
  margin: 0;
  font-size: 1.35rem;
  letter-spacing: 0.01em;
}
.header p {
  margin: 8px 0 0;
  color: var(--muted);
  font-size: 0.95rem;
}
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 14px;
  padding: 18px;
}
.field {
  display: flex;
  flex-direction: column;
  gap: 6px;
}
label {
  font-size: 0.82rem;
  font-weight: 700;
  color: #355448;
  text-transform: uppercase;
  letter-spacing: 0.04em;
}
input, select {
  border: 1px solid #c8dbd1;
  border-radius: 10px;
  padding: 10px 11px;
  font-size: 0.96rem;
  background: #fbfdfc;
  color: var(--ink);
}
input:focus, select:focus {
  border-color: var(--accent);
  outline: none;
  box-shadow: 0 0 0 3px rgba(0, 122, 90, 0.16);
}
.footer {
  padding: 16px 18px 20px;
  border-top: 1px solid var(--line);
  display: flex;
  gap: 12px;
  align-items: center;
  justify-content: space-between;
  flex-wrap: wrap;
}
button {
  border: 0;
  background: linear-gradient(130deg, var(--accent), var(--accent-2));
  color: white;
  font-weight: 700;
  letter-spacing: 0.02em;
  padding: 11px 18px;
  border-radius: 10px;
  cursor: pointer;
}
.status {
  font-size: 0.92rem;
  color: var(--muted);
}
@media (max-width: 560px) {
  .wrap { padding: 14px 10px 28px; }
  .header h1 { font-size: 1.18rem; }
}
</style>
</head>
<body>
<div class="wrap">
  <div class="card">
    <div class="header">
      <h1>Fishy Catchy Configuration</h1>
      <p>Adjust detection, LED behavior, and WiFi power settings, then save once.</p>
    </div>
    <form id="cfgForm">
      <div class="grid">
        <div class="field"><label for="wifi_ssid">WiFi Name</label><input id="wifi_ssid" name="wifi_ssid" maxlength="32" required /></div>
        <div class="field"><label for="wifi_password">WiFi Password</label><input id="wifi_password" name="wifi_password" maxlength="64" /></div>
        <div class="field"><label for="wifi_shutdown_delay_s">WiFi Shutdown Delay (s)</label><input id="wifi_shutdown_delay_s" name="wifi_shutdown_delay_s" type="number" min="10" max="1800" required /></div>
        <div class="field"><label for="sensor_period_ms">Sensor Polling (ms)</label><input id="sensor_period_ms" name="sensor_period_ms" type="number" min="5" max="1000" required /></div>
        <div class="field"><label for="queue_length">Queue Length</label><input id="queue_length" name="queue_length" type="number" min="8" max="128" required /></div>
        <div class="field"><label for="led_brightness">LED Brightness (0-255)</label><input id="led_brightness" name="led_brightness" type="number" min="0" max="255" required /></div>
        <div class="field"><label for="led_idle_pattern">LED Idle Pattern</label><select id="led_idle_pattern" name="led_idle_pattern"><option value="0">Solid</option><option value="1">Breath</option><option value="2">Chase</option><option value="3">Pulse</option><option value="4">Rainbow</option></select></div>
        <div class="field"><label for="led_wifi_pattern">LED WiFi Pattern</label><select id="led_wifi_pattern" name="led_wifi_pattern"><option value="0">Solid</option><option value="1">Breath</option><option value="2">Chase</option><option value="3">Pulse</option><option value="4">Rainbow</option></select></div>
        <div class="field"><label for="led_catch_pattern">LED Catch Pattern</label><select id="led_catch_pattern" name="led_catch_pattern"><option value="0">Solid</option><option value="1">Breath</option><option value="2">Chase</option><option value="3">Pulse</option><option value="4">Rainbow</option></select></div>
        <div class="field"><label for="algorithm">Detection Algorithm</label><select id="algorithm" name="algorithm"><option value="0">Cartesian Spike Once</option><option value="1">Cartesian Dense Spikes</option><option value="2">Cumulative Magnitude</option></select></div>
        <div class="field"><label for="single_spike_threshold">Single Spike Threshold</label><input id="single_spike_threshold" name="single_spike_threshold" type="number" min="1000" step="1000" /></div>
        <div class="field"><label for="dense_spike_threshold">Dense Spike Threshold</label><input id="dense_spike_threshold" name="dense_spike_threshold" type="number" min="1000" step="1000" /></div>
        <div class="field"><label for="dense_window_samples">Dense Window Samples</label><input id="dense_window_samples" name="dense_window_samples" type="number" min="4" max="256" /></div>
        <div class="field"><label for="dense_required_hits">Dense Required Hits</label><input id="dense_required_hits" name="dense_required_hits" type="number" min="1" max="256" /></div>
        <div class="field"><label for="cumulative_threshold">Cumulative Threshold</label><input id="cumulative_threshold" name="cumulative_threshold" type="number" min="1000" step="1000" /></div>
        <div class="field"><label for="cumulative_window_samples">Cumulative Window Samples</label><input id="cumulative_window_samples" name="cumulative_window_samples" type="number" min="4" max="256" /></div>
        <div class="field"><label for="catch_cooldown_ms">Catch Cooldown (ms)</label><input id="catch_cooldown_ms" name="catch_cooldown_ms" type="number" min="100" max="20000" /></div>
      </div>
      <div class="footer">
        <button type="submit">Save and Turn Off WiFi</button>
        <div id="status" class="status">Loading current configuration...</div>
      </div>
    </form>
  </div>
</div>
<script>
const fields = [
  "wifi_ssid", "wifi_password", "wifi_shutdown_delay_s", "sensor_period_ms", "queue_length",
  "led_brightness", "led_idle_pattern", "led_wifi_pattern", "led_catch_pattern", "algorithm",
  "single_spike_threshold", "dense_spike_threshold", "dense_window_samples", "dense_required_hits",
  "cumulative_threshold", "cumulative_window_samples", "catch_cooldown_ms"
];

async function loadConfig() {
  const resp = await fetch('/config');
  const cfg = await resp.json();
  fields.forEach((k) => {
    const el = document.getElementById(k);
    if (el && cfg[k] !== undefined) el.value = cfg[k];
  });
  document.getElementById('status').textContent = 'Ready.';
}

async function keepAlive() {
  try {
    await fetch('/state');
  } catch (_) {}
}

document.getElementById('cfgForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const form = new FormData(e.target);
  const body = new URLSearchParams(form).toString();

  document.getElementById('status').textContent = 'Saving...';
  const resp = await fetch('/save', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body
  });

  if (resp.ok) {
    document.getElementById('status').textContent = 'Saved. WiFi is shutting down now.';
  } else {
    document.getElementById('status').textContent = 'Save failed. Check values and retry.';
  }
});

loadConfig().catch(() => {
  document.getElementById('status').textContent = 'Failed to load current config.';
});
setInterval(keepAlive, 2000);
</script>
</body>
</html>)HTML";

inline uint32_t NowMs() {
  return pdTICKS_TO_MS(xTaskGetTickCount());
}

void MarkActivity(WifiTaskContext* ctx) {
  uint32_t now = NowMs();
  ctx->last_activity_tick_ms = now;
  SystemState_MarkWebActivity(ctx->state_store, now);
}

int HexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

void UrlDecode(char* str) {
  char* src = str;
  char* dst = str;

  while (*src != '\0') {
    if (*src == '%') {
      int hi = HexVal(*(src + 1));
      int lo = HexVal(*(src + 2));
      if (hi >= 0 && lo >= 0) {
        *dst++ = static_cast<char>((hi << 4) | lo);
        src += 3;
        continue;
      }
    }
    if (*src == '+') {
      *dst++ = ' ';
    } else {
      *dst++ = *src;
    }
    ++src;
  }

  *dst = '\0';
}

bool ParseUInt(const char* value, uint32_t* out) {
  if (value == nullptr || out == nullptr) {
    return false;
  }

  while (*value != '\0' && isspace(static_cast<unsigned char>(*value))) {
    ++value;
  }

  if (*value == '\0') {
    return false;
  }

  char* end = nullptr;
  unsigned long parsed = strtoul(value, &end, 10);
  if (end == value) {
    return false;
  }

  *out = static_cast<uint32_t>(parsed);
  return true;
}

void ApplyField(AppConfig* cfg, const char* key, const char* value) {
  if (strcmp(key, "wifi_ssid") == 0) {
    strncpy(cfg->wifi_ssid, value, sizeof(cfg->wifi_ssid) - 1);
    cfg->wifi_ssid[sizeof(cfg->wifi_ssid) - 1] = '\0';
    return;
  }
  if (strcmp(key, "wifi_password") == 0) {
    strncpy(cfg->wifi_password, value, sizeof(cfg->wifi_password) - 1);
    cfg->wifi_password[sizeof(cfg->wifi_password) - 1] = '\0';
    return;
  }

  uint32_t n = 0;
  if (!ParseUInt(value, &n)) {
    return;
  }

  if (strcmp(key, "wifi_shutdown_delay_s") == 0) cfg->wifi_shutdown_delay_s = static_cast<uint16_t>(n);
  else if (strcmp(key, "sensor_period_ms") == 0) cfg->sensor_period_ms = static_cast<uint16_t>(n);
  else if (strcmp(key, "queue_length") == 0) cfg->queue_length = static_cast<uint16_t>(n);
  else if (strcmp(key, "led_brightness") == 0) cfg->led_brightness = static_cast<uint8_t>(n);
  else if (strcmp(key, "led_idle_pattern") == 0) cfg->led_idle_pattern = static_cast<uint8_t>(n);
  else if (strcmp(key, "led_wifi_pattern") == 0) cfg->led_wifi_pattern = static_cast<uint8_t>(n);
  else if (strcmp(key, "led_catch_pattern") == 0) cfg->led_catch_pattern = static_cast<uint8_t>(n);
  else if (strcmp(key, "algorithm") == 0) cfg->algorithm = static_cast<uint8_t>(n);
  else if (strcmp(key, "single_spike_threshold") == 0) cfg->single_spike_threshold = n;
  else if (strcmp(key, "dense_spike_threshold") == 0) cfg->dense_spike_threshold = n;
  else if (strcmp(key, "dense_window_samples") == 0) cfg->dense_window_samples = static_cast<uint16_t>(n);
  else if (strcmp(key, "dense_required_hits") == 0) cfg->dense_required_hits = static_cast<uint16_t>(n);
  else if (strcmp(key, "cumulative_threshold") == 0) cfg->cumulative_threshold = n;
  else if (strcmp(key, "cumulative_window_samples") == 0) cfg->cumulative_window_samples = static_cast<uint16_t>(n);
  else if (strcmp(key, "catch_cooldown_ms") == 0) cfg->catch_cooldown_ms = static_cast<uint16_t>(n);
}

void ParseFormEncoded(char* body, AppConfig* cfg) {
  char* token = strtok(body, "&");
  while (token != nullptr) {
    char* eq = strchr(token, '=');
    if (eq != nullptr) {
      *eq = '\0';
      char* key = token;
      char* value = eq + 1;
      UrlDecode(key);
      UrlDecode(value);
      ApplyField(cfg, key, value);
    }
    token = strtok(nullptr, "&");
  }
}

esp_err_t HandleRoot(httpd_req_t* req) {
  auto* ctx = static_cast<WifiTaskContext*>(req->user_ctx);
  MarkActivity(ctx);
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HandleConfig(httpd_req_t* req) {
  auto* ctx = static_cast<WifiTaskContext*>(req->user_ctx);
  MarkActivity(ctx);

  AppConfig cfg = {};
  uint32_t version = 0;
  if (!ConfigStore_GetCopy(ctx->config_store, &cfg, &version)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    return ESP_FAIL;
  }

  char body[1024];
  int len = snprintf(
      body, sizeof(body),
      "{\"version\":%lu,\"wifi_ssid\":\"%s\",\"wifi_password\":\"%s\","
      "\"wifi_shutdown_delay_s\":%u,\"sensor_period_ms\":%u,\"queue_length\":%u,"
      "\"led_brightness\":%u,\"led_idle_pattern\":%u,\"led_wifi_pattern\":%u,"
      "\"led_catch_pattern\":%u,\"algorithm\":%u,\"single_spike_threshold\":%lu,"
      "\"dense_spike_threshold\":%lu,\"dense_window_samples\":%u,"
      "\"dense_required_hits\":%u,\"cumulative_threshold\":%lu,"
      "\"cumulative_window_samples\":%u,\"catch_cooldown_ms\":%u}",
      static_cast<unsigned long>(version), cfg.wifi_ssid, cfg.wifi_password,
      cfg.wifi_shutdown_delay_s, cfg.sensor_period_ms, cfg.queue_length,
      cfg.led_brightness, cfg.led_idle_pattern, cfg.led_wifi_pattern,
      cfg.led_catch_pattern, cfg.algorithm,
      static_cast<unsigned long>(cfg.single_spike_threshold),
      static_cast<unsigned long>(cfg.dense_spike_threshold),
      cfg.dense_window_samples, cfg.dense_required_hits,
      static_cast<unsigned long>(cfg.cumulative_threshold),
      cfg.cumulative_window_samples, cfg.catch_cooldown_ms);

  if (len <= 0 || len >= static_cast<int>(sizeof(body))) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "serialization failed");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, body, len);
}

esp_err_t HandleState(httpd_req_t* req) {
  auto* ctx = static_cast<WifiTaskContext*>(req->user_ctx);
  MarkActivity(ctx);

  SystemState state = {};
  if (!SystemState_GetCopy(ctx->state_store, &state)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "state unavailable");
    return ESP_FAIL;
  }

  char body[192];
  int len = snprintf(body, sizeof(body),
                     "{\"fish_caught\":%s,\"catch_count\":%lu,\"wifi_enabled\":%s}",
                     state.fish_caught_latched ? "true" : "false",
                     static_cast<unsigned long>(state.fish_catch_count),
                     state.wifi_enabled ? "true" : "false");
  if (len <= 0 || len >= static_cast<int>(sizeof(body))) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "serialization failed");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, body, len);
}

esp_err_t HandleSave(httpd_req_t* req) {
  auto* ctx = static_cast<WifiTaskContext*>(req->user_ctx);
  MarkActivity(ctx);

  if (req->content_len <= 0 || req->content_len > 2048) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid payload size");
    return ESP_FAIL;
  }

  char* body = static_cast<char*>(calloc(1, req->content_len + 1));
  if (body == nullptr) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    return ESP_FAIL;
  }

  int received = 0;
  while (received < req->content_len) {
    int r = httpd_req_recv(req, body + received, req->content_len - received);
    if (r <= 0) {
      free(body);
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read error");
      return ESP_FAIL;
    }
    received += r;
  }

  AppConfig cfg = {};
  uint32_t version = 0;
  if (!ConfigStore_GetCopy(ctx->config_store, &cfg, &version)) {
    free(body);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    return ESP_FAIL;
  }

  ParseFormEncoded(body, &cfg);
  free(body);

  if (!ConfigStore_UpdateAndPersist(ctx->config_store, &cfg)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "persist failed");
    return ESP_FAIL;
  }

  ctx->shutdown_requested = true;
  SystemState_SetWifi(ctx->state_store, true, false);

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"saved\"}");
}

bool StartServer(WifiTaskContext* ctx) {
  httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
  conf.server_port = 80;
  conf.max_uri_handlers = 8;

  if (httpd_start(&ctx->server, &conf) != ESP_OK) {
    ESP_LOGE(kTag, "httpd_start failed");
    return false;
  }

  httpd_uri_t root = {"/", HTTP_GET, HandleRoot, ctx};
  httpd_uri_t cfg = {"/config", HTTP_GET, HandleConfig, ctx};
  httpd_uri_t state = {"/state", HTTP_GET, HandleState, ctx};
  httpd_uri_t save = {"/save", HTTP_POST, HandleSave, ctx};

  httpd_register_uri_handler(ctx->server, &root);
  httpd_register_uri_handler(ctx->server, &cfg);
  httpd_register_uri_handler(ctx->server, &state);
  httpd_register_uri_handler(ctx->server, &save);
  return true;
}

bool StartWifiAp(WifiTaskContext* ctx, const AppConfig& cfg) {
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "esp_netif_init failed: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
    return false;
  }

  ctx->ap_netif = esp_netif_create_default_wifi_ap();
  if (ctx->ap_netif == nullptr) {
    ESP_LOGE(kTag, "esp_netif_create_default_wifi_ap failed");
    return false;
  }

  wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&wifi_init);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_wifi_init failed: %s", esp_err_to_name(err));
    return false;
  }

  wifi_config_t ap_cfg = {};
  strncpy(reinterpret_cast<char*>(ap_cfg.ap.ssid), cfg.wifi_ssid, sizeof(ap_cfg.ap.ssid) - 1);
  strncpy(reinterpret_cast<char*>(ap_cfg.ap.password), cfg.wifi_password,
          sizeof(ap_cfg.ap.password) - 1);
  ap_cfg.ap.ssid_len = strlen(cfg.wifi_ssid);
  ap_cfg.ap.channel = 1;
  ap_cfg.ap.max_connection = 4;
  ap_cfg.ap.beacon_interval = 100;
  ap_cfg.ap.authmode = strlen(cfg.wifi_password) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

  err = esp_wifi_set_mode(WIFI_MODE_AP);
  if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
  if (err == ESP_OK) err = esp_wifi_start();

  if (err != ESP_OK) {
    ESP_LOGE(kTag, "WiFi AP start failed: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(kTag, "AP started: SSID=%s", cfg.wifi_ssid);
  return true;
}

void StopWifiAp(WifiTaskContext* ctx) {
  if (ctx->server != nullptr) {
    httpd_stop(ctx->server);
    ctx->server = nullptr;
  }

  esp_wifi_stop();
  esp_wifi_deinit();

  if (ctx->ap_netif != nullptr) {
    esp_netif_destroy(ctx->ap_netif);
    ctx->ap_netif = nullptr;
  }

  SystemState_SetWifi(ctx->state_store, false, false);
  ESP_LOGI(kTag, "WiFi AP stopped");
}

void WifiTaskEntry(void* parameter) {
  WifiTaskContext* ctx = static_cast<WifiTaskContext*>(parameter);

  AppConfig cfg = {};
  uint32_t version = 0;
  if (!ConfigStore_GetCopy(ctx->config_store, &cfg, &version)) {
    ESP_LOGE(kTag, "Failed to read config at startup");
    vTaskDelete(nullptr);
    return;
  }

  if (!StartWifiAp(ctx, cfg)) {
    vTaskDelete(nullptr);
    return;
  }
  if (!StartServer(ctx)) {
    StopWifiAp(ctx);
    vTaskDelete(nullptr);
    return;
  }

  ctx->startup_tick_ms = NowMs();
  ctx->last_activity_tick_ms = ctx->startup_tick_ms;
  ctx->shutdown_requested = false;

  SystemState_SetWifi(ctx->state_store, true, false);

  for (;;) {
    AppConfig latest_cfg = {};
    uint32_t latest_version = 0;
    uint16_t shutdown_delay_s = cfg.wifi_shutdown_delay_s;
    if (ConfigStore_GetCopy(ctx->config_store, &latest_cfg, &latest_version)) {
      cfg = latest_cfg;
      shutdown_delay_s = cfg.wifi_shutdown_delay_s;
    }

    uint32_t now_ms = NowMs();
    uint32_t idle_ms = now_ms - ctx->last_activity_tick_ms;
    uint32_t shutdown_ms = static_cast<uint32_t>(shutdown_delay_s) * 1000U;

    if (ctx->shutdown_requested) {
      ESP_LOGI(kTag, "Save completed, shutting WiFi down now");
      break;
    }

    if (idle_ms >= shutdown_ms) {
      ESP_LOGI(kTag, "WiFi idle timeout reached (%lu ms)", static_cast<unsigned long>(idle_ms));
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }

  StopWifiAp(ctx);
  vTaskDelete(nullptr);
}
}  // namespace

bool WifiTask_Start(SharedConfigStore* config_store, SharedSystemState* state_store) {
  if (config_store == nullptr || state_store == nullptr) {
    return false;
  }

  static WifiTaskContext ctx = {};
  ctx.config_store = config_store;
  ctx.state_store = state_store;

  BaseType_t ok = xTaskCreatePinnedToCore(WifiTaskEntry, "WifiTask", 12288, &ctx, 4,
                                          nullptr, 0);
  return ok == pdPASS;
}
