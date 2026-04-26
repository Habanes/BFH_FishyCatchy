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
  --bg: #f3faf6;
  --card: #ffffff;
  --ink: #17352a;
  --muted: #5a7067;
  --line: #d9e6df;
  --accent: #007a5a;
  --accent2: #0fa36f;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  color: var(--ink);
  font-family: "Avenir Next", "Segoe UI", sans-serif;
  background: linear-gradient(160deg, #f7fbf8, var(--bg));
}
.wrap {
  max-width: 600px;
  margin: 0 auto;
  padding: 20px 12px 40px;
}
.card {
  background: var(--card);
  border: 1px solid var(--line);
  border-radius: 16px;
  overflow: hidden;
  box-shadow: 0 10px 28px rgba(20, 58, 43, 0.08);
}
.header {
  padding: 18px;
  border-bottom: 1px solid var(--line);
  background: linear-gradient(120deg, #ffffff, #eef6f1);
}
.header h1 { margin: 0; font-size: 1.25rem; }
.header p { margin: 8px 0 0; color: var(--muted); }
.tabs {
  display: flex;
  gap: 8px;
  padding: 12px 14px;
  border-bottom: 1px solid var(--line);
}
.tab {
  border: 1px solid #bdd7cb;
  background: #f8fcfa;
  color: #285242;
  border-radius: 10px;
  padding: 8px 12px;
  font-weight: 700;
  cursor: pointer;
}
.tab.active {
  border-color: var(--accent);
  background: #e8f8f2;
  color: #0d5e44;
}
.panel { display: none; }
.panel.active { display: block; }
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 12px;
  padding: 16px;
}
.field { display: flex; flex-direction: column; gap: 6px; }
label {
  font-size: 0.8rem;
  font-weight: 700;
  color: #355448;
  text-transform: uppercase;
  letter-spacing: 0.04em;
}
input, select {
  border: 1px solid #c8dbd1;
  border-radius: 10px;
  padding: 10px 11px;
  background: #fbfdfc;
  color: var(--ink);
}
input:focus, select:focus {
  outline: none;
  border-color: var(--accent);
  box-shadow: 0 0 0 3px rgba(0, 122, 90, 0.15);
}
.rangeLine { display: flex; gap: 8px; align-items: center; }
.rangeLine input[type="range"] { flex: 1; }
.rangeLine .val {
  width: 80px;
  text-align: right;
  color: #2b5042;
  font-weight: 700;
}
.sensorGrid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 12px;
  padding: 16px;
}
.sensorCard {
  border: 1px solid var(--line);
  border-radius: 12px;
  padding: 12px;
  background: #fbfdfc;
}
.sensorCard h3 { margin: 0 0 8px; font-size: 0.95rem; }
.sensorRow {
  display: flex;
  justify-content: space-between;
  gap: 10px;
  padding: 4px 0;
  border-bottom: 1px dashed #dbe9e2;
}
.sensorRow:last-child { border-bottom: 0; }
.footer {
  border-top: 1px solid var(--line);
  padding: 14px 16px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}
button {
  border: 0;
  border-radius: 10px;
  padding: 11px 18px;
  color: white;
  font-weight: 700;
  cursor: pointer;
  background: linear-gradient(130deg, var(--accent), var(--accent2));
}
.status { color: var(--muted); font-size: 0.92rem; }
</style>
</head>
<body>
<div class="wrap">
  <div class="card">
    <div class="header">
      <h1>Fishy Catchy Configuration</h1>
      <p>Adjust detection settings while WiFi AP is active.</p>
    </div>

    <div class="tabs">
      <button class="tab active" data-tab="settings" type="button">Settings</button>
      <button class="tab" data-tab="live" type="button">Live Sensor</button>
    </div>

    <div id="settings" class="panel active">
      <form id="cfgForm">
        <div class="grid">
          <div class="field"><label for="algorithm">Algorithm</label><select id="algorithm" name="algorithm"><option value="0">Single Peak</option><option value="1">Cumulative Avg</option></select></div>
          
          <div class="field">
            <label for="single_spike_threshold">Single Peak (g sum)</label>
            <div class="rangeLine"><input id="single_spike_threshold" name="single_spike_threshold" type="range" min="0.1" max="12" step="0.1" /><span class="val" id="single_spike_threshold_val">0</span></div>
          </div>
          
          <div class="field">
            <label for="cumulative_threshold">Cumulative Threshold</label>
            <div class="rangeLine"><input id="cumulative_threshold" name="cumulative_threshold" type="range" min="0.2" max="20" step="0.1" /><span class="val" id="cumulative_threshold_val">0</span></div>
          </div>
          
          <div class="field"><label for="cumulative_window_ms">Window Time (ms)</label><input id="cumulative_window_ms" name="cumulative_window_ms" type="number" min="100" max="60000" step="100" required /></div>
          
          <div class="field"><label for="catch_cooldown_ms">Catch Cooldown (ms)</label><input id="catch_cooldown_ms" name="catch_cooldown_ms" type="number" min="100" max="20000" step="100" required /></div>
          
          <div class="field"><label for="led_brightness">LED Brightness</label><input id="led_brightness" name="led_brightness" type="number" min="0" max="255" required /></div>
        </div>

        <div class="footer">
          <button type="submit">Save and Turn Off WiFi</button>
          <div id="status" class="status">Loading configuration...</div>
        </div>
      </form>
    </div>

    <div id="live" class="panel">
      <div class="sensorGrid">
        <div class="sensorCard">
          <h3>Accelerometer (g)</h3>
          <div class="sensorRow"><span>X</span><strong id="ax">-</strong></div>
          <div class="sensorRow"><span>Y</span><strong id="ay">-</strong></div>
          <div class="sensorRow"><span>Z</span><strong id="az">-</strong></div>
          <div class="sensorRow"><span>Valid</span><strong id="accel_valid">-</strong></div>
        </div>
        <div class="sensorCard">
          <h3>Detection</h3>
          <div class="sensorRow"><span>Status</span><strong id="calc_detected">-</strong></div>
          <div class="sensorRow"><span>Abs Sum</span><strong id="calc_abs_sum">-</strong></div>
          <div class="sensorRow"><span>Avg Value</span><strong id="calc_cumulative_sum">-</strong></div>
          <div class="sensorRow"><span>Algorithm</span><strong id="calc_algorithm">-</strong></div>
        </div>
      </div>
      <div class="footer">
        <div class="status">Live updates every 300 ms</div>
      </div>
    </div>
  </div>
</div>

<script>
const saveFields = [
  'algorithm','single_spike_threshold','cumulative_threshold',
  'cumulative_window_ms','catch_cooldown_ms','led_brightness'
];

function setTab(tabName) {
  document.querySelectorAll('.tab').forEach(t => t.classList.toggle('active', t.dataset.tab === tabName));
  document.querySelectorAll('.panel').forEach(p => p.classList.toggle('active', p.id === tabName));
}

function fmt(v, digits=3) {
  if (v === null || v === undefined || Number.isNaN(v)) return '-';
  return Number(v).toFixed(digits);
}

function algoName(v) {
  if (v === 0) return 'Single Peak';
  if (v === 1) return 'Cumulative Avg';
  return '-';
}

function linkRange(id) {
  const range = document.getElementById(id);
  const out = document.getElementById(id + '_val');
  const refresh = () => out.textContent = Number(range.value).toFixed(1);
  range.addEventListener('input', refresh);
  refresh();
}

async function loadConfig() {
  const resp = await fetch('/config');
  const cfg = await resp.json();

  saveFields.forEach((key) => {
    const el = document.getElementById(key);
    if (el && cfg[key] !== undefined) el.value = cfg[key];
  });

  linkRange('single_spike_threshold');
  linkRange('cumulative_threshold');
  document.getElementById('status').textContent = 'Ready.';
}

async function updateLive() {
  try {
    const sensorResp = await fetch('/sensor');
    const sensor = await sensorResp.json();

    document.getElementById('ax').textContent = fmt(sensor.ax);
    document.getElementById('ay').textContent = fmt(sensor.ay);
    document.getElementById('az').textContent = fmt(sensor.az);
    document.getElementById('accel_valid').textContent = sensor.accel_valid ? 'yes' : 'no';
    document.getElementById('calc_abs_sum').textContent = fmt(sensor.calc_abs_sum);
    document.getElementById('calc_cumulative_sum').textContent = fmt(sensor.calc_cumulative_sum);
    document.getElementById('calc_detected').textContent = sensor.calc_detected ? 'DETECTED' : 'waiting';
    document.getElementById('calc_algorithm').textContent = algoName(sensor.calc_algorithm);
  } catch (_) {
  }
}

document.getElementById('cfgForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const form = new FormData(e.target);
  const keepOnlyEditable = new URLSearchParams();
  saveFields.forEach((key) => keepOnlyEditable.append(key, form.get(key)));

  document.getElementById('status').textContent = 'Saving...';
  const resp = await fetch('/save', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: keepOnlyEditable.toString()
  });

  if (resp.ok) {
    document.getElementById('status').textContent = 'Saved. WiFi will shut down now.';
  } else {
    document.getElementById('status').textContent = 'Save failed. Check values and retry.';
  }
});

document.querySelectorAll('.tab').forEach((btn) => {
  btn.addEventListener('click', () => setTab(btn.dataset.tab));
});

loadConfig().catch(() => {
  document.getElementById('status').textContent = 'Failed to load configuration.';
});
setInterval(updateLive, 300);
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

bool ParseFloat(const char* value, float* out) {
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
  float parsed = strtof(value, &end);
  if (end == value) {
    return false;
  }

  *out = parsed;
  return true;
}

void ApplyField(AppConfig* cfg, const char* key, const char* value) {
  if (strcmp(key, "cumulative_threshold") == 0 || strcmp(key, "single_spike_threshold") == 0) {
    float v = 0.0f;
    if (ParseFloat(value, &v)) {
      if (strcmp(key, "cumulative_threshold") == 0) {
        cfg->cumulative_threshold = v;
      } else {
        cfg->single_spike_threshold = v;
      }
    }
    return;
  }

  uint32_t n = 0;
  if (!ParseUInt(value, &n)) {
    return;
  }

  if (strcmp(key, "algorithm") == 0) cfg->algorithm = static_cast<uint8_t>(n);
  else if (strcmp(key, "led_brightness") == 0) cfg->led_brightness = static_cast<uint8_t>(n);
  else if (strcmp(key, "cumulative_window_ms") == 0) cfg->cumulative_window_ms = static_cast<uint16_t>(n);
  else if (strcmp(key, "catch_cooldown_ms") == 0) cfg->catch_cooldown_ms = static_cast<uint16_t>(n);
  else if (strcmp(key, "cumulative_window_samples") == 0) cfg->cumulative_window_ms = static_cast<uint16_t>(n);  // legacy
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

  char body[512];
  int len = snprintf(
      body, sizeof(body),
      "{\"version\":%lu,\"algorithm\":%u,\"led_brightness\":%u,"
      "\"single_spike_threshold\":%.2f,\"cumulative_threshold\":%.2f,"
      "\"cumulative_window_ms\":%u,\"catch_cooldown_ms\":%u}",
      static_cast<unsigned long>(version),
      static_cast<unsigned int>(cfg.algorithm),
      cfg.led_brightness,
      static_cast<double>(cfg.single_spike_threshold),
      static_cast<double>(cfg.cumulative_threshold),
      cfg.cumulative_window_ms,
      cfg.catch_cooldown_ms);

  if (len <= 0 || len >= static_cast<int>(sizeof(body))) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "serialization failed");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, body, len);
}

esp_err_t HandleSensor(httpd_req_t* req) {
  auto* ctx = static_cast<WifiTaskContext*>(req->user_ctx);
  MarkActivity(ctx);

  SystemState state = {};
  if (!SystemState_GetCopy(ctx->state_store, &state)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "state unavailable");
    return ESP_FAIL;
  }

  char body[512];
  int len = snprintf(
      body, sizeof(body),
      "{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
      "\"accel_valid\":%s,"
      "\"calc_abs_sum\":%.3f,\"calc_cumulative_sum\":%.3f,"
      "\"calc_detected\":%s,\"calc_algorithm\":%u}",
      static_cast<double>(state.last_ax), static_cast<double>(state.last_ay),
      static_cast<double>(state.last_az),
      state.last_accel_valid ? "true" : "false",
      static_cast<double>(state.calc_abs_axis_sum),
      static_cast<double>(state.calc_cumulative_sum),
      state.calc_detected ? "true" : "false",
      static_cast<unsigned int>(state.calc_algorithm));

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
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"saved\"}");
}

bool StartServer(WifiTaskContext* ctx) {
  httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
  conf.server_port = 80;
  conf.max_uri_handlers = 10;

  if (httpd_start(&ctx->server, &conf) != ESP_OK) {
    ESP_LOGE(kTag, "httpd_start failed");
    return false;
  }

  httpd_uri_t root = {"/", HTTP_GET, HandleRoot, ctx};
  httpd_uri_t cfg = {"/config", HTTP_GET, HandleConfig, ctx};
  httpd_uri_t sensor = {"/sensor", HTTP_GET, HandleSensor, ctx};
  httpd_uri_t save = {"/save", HTTP_POST, HandleSave, ctx};

  httpd_register_uri_handler(ctx->server, &root);
  httpd_register_uri_handler(ctx->server, &cfg);
  httpd_register_uri_handler(ctx->server, &sensor);
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

  constexpr const char* kSsid = "FishyCatchy";
  constexpr const char* kPassword = "fishycatchy";

  wifi_config_t ap_cfg = {};
  strncpy(reinterpret_cast<char*>(ap_cfg.ap.ssid), kSsid, sizeof(ap_cfg.ap.ssid) - 1);
  strncpy(reinterpret_cast<char*>(ap_cfg.ap.password), kPassword,
          sizeof(ap_cfg.ap.password) - 1);
  ap_cfg.ap.ssid_len = strlen(kSsid);
  ap_cfg.ap.channel = 1;
  ap_cfg.ap.max_connection = 4;
  ap_cfg.ap.beacon_interval = 100;
  ap_cfg.ap.authmode = strlen(kPassword) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

  err = esp_wifi_set_mode(WIFI_MODE_AP);
  if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
  if (err == ESP_OK) err = esp_wifi_start();

  if (err != ESP_OK) {
    ESP_LOGE(kTag, "WiFi AP start failed: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(kTag, "AP started: SSID=%s", kSsid);
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
  uint32_t cfg_version = 0;
  if (!ConfigStore_GetCopy(ctx->config_store, &cfg, &cfg_version)) {
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
    if (ConfigStore_GetCopy(ctx->config_store, &latest_cfg, &latest_version) &&
        latest_version != cfg_version) {
      cfg = latest_cfg;
      cfg_version = latest_version;
    }

    uint32_t now_ms = NowMs();
    uint32_t idle_ms = now_ms - ctx->last_activity_tick_ms;
    constexpr uint32_t kWifiShutdownDelayMs = 120000;

    if (ctx->shutdown_requested) {
      ESP_LOGI(kTag, "Save completed, shutting WiFi down now");
      break;
    }

    if (idle_ms >= kWifiShutdownDelayMs) {
      ESP_LOGI(kTag, "WiFi idle timeout reached (%lu ms)", static_cast<unsigned long>(idle_ms));
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(250));
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

  BaseType_t ok = xTaskCreatePinnedToCore(WifiTaskEntry, "WifiTask", 16384, &ctx, 4,
                                          nullptr, 0);
  return ok == pdPASS;
}
