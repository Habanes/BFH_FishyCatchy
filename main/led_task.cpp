#include "led_task.hpp"

#include <stdint.h>

#include "esp_log.h"
#include "led_strip.h"

namespace {
constexpr char kTag[] = "LedTask";
constexpr uint32_t kFrameDelayMs = 40;
constexpr uint32_t kCatchVisualMs = 5000;

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct LedTaskContext {
  SharedConfigStore* config_store;
  SharedSystemState* state_store;
};

uint8_t Scale(uint8_t value, uint8_t brightness) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * brightness) / 255U);
}

uint8_t TriangleWave(uint32_t frame, uint16_t period_frames, uint8_t min_value,
                     uint8_t max_value) {
  if (period_frames < 2 || min_value >= max_value) {
    return max_value;
  }

  uint16_t phase = frame % period_frames;
  uint16_t half = period_frames / 2;
  uint16_t span = static_cast<uint16_t>(max_value - min_value);

  if (phase <= half) {
    return static_cast<uint8_t>(min_value + (span * phase) / half);
  }

  return static_cast<uint8_t>(max_value - (span * (phase - half)) / half);
}

Rgb HsvToRgb(uint16_t hue, uint8_t sat, uint8_t val) {
  uint8_t region = static_cast<uint8_t>((hue / 60U) % 6U);
  uint16_t remainder = static_cast<uint16_t>((hue % 60U) * 255U / 60U);

  uint8_t p = static_cast<uint8_t>((val * (255U - sat)) / 255U);
  uint8_t q = static_cast<uint8_t>((val * (255U - ((sat * remainder) / 255U))) / 255U);
  uint8_t t = static_cast<uint8_t>((val * (255U - ((sat * (255U - remainder)) / 255U))) / 255U);

  switch (region) {
    case 0:
      return {val, t, p};
    case 1:
      return {q, val, p};
    case 2:
      return {p, val, t};
    case 3:
      return {p, q, val};
    case 4:
      return {t, p, val};
    default:
      return {val, p, q};
  }
}

void SetAll(led_strip_handle_t strip, Rgb color, uint8_t brightness) {
  for (uint16_t i = 0; i < kLedCount; ++i) {
    led_strip_set_pixel(strip, i, Scale(color.r, brightness), Scale(color.g, brightness),
                        Scale(color.b, brightness));
  }
}

void RenderPattern(led_strip_handle_t strip, LedPattern pattern, const Rgb& base,
                   uint8_t brightness, uint32_t frame) {
  switch (pattern) {
    case LedPattern::kSolid:
      SetAll(strip, base, brightness);
      break;

    case LedPattern::kBreath: {
      uint8_t level = TriangleWave(frame, 80, 24, brightness);
      SetAll(strip, base, level);
      break;
    }

    case LedPattern::kChase: {
      uint16_t active = frame % kLedCount;
      for (uint16_t i = 0; i < kLedCount; ++i) {
        uint8_t level = (i == active) ? brightness : static_cast<uint8_t>(brightness / 10U);
        led_strip_set_pixel(strip, i, Scale(base.r, level), Scale(base.g, level),
                            Scale(base.b, level));
      }
      break;
    }

    case LedPattern::kPulse: {
      uint8_t level = TriangleWave(frame, 28, 16, brightness);
      SetAll(strip, base, level);
      break;
    }

    case LedPattern::kRainbow:
    default: {
      uint16_t shift = static_cast<uint16_t>((frame * 8U) % 360U);
      for (uint16_t i = 0; i < kLedCount; ++i) {
        uint16_t hue = static_cast<uint16_t>((shift + (i * 360U / kLedCount)) % 360U);
        Rgb rgb = HsvToRgb(hue, 255, brightness);
        led_strip_set_pixel(strip, i, rgb.r, rgb.g, rgb.b);
      }
      break;
    }
  }

  led_strip_refresh(strip);
}

void LedTaskEntry(void* parameter) {
  LedTaskContext* ctx = static_cast<LedTaskContext*>(parameter);

  led_strip_config_t strip_cfg = {};
  strip_cfg.strip_gpio_num = static_cast<gpio_num_t>(BoardPins::kLedData);
  strip_cfg.max_leds = kLedCount;
  strip_cfg.led_model = LED_MODEL_WS2812;
  strip_cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

  led_strip_rmt_config_t rmt_cfg = {};
  rmt_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  rmt_cfg.resolution_hz = 10 * 1000 * 1000;
  rmt_cfg.mem_block_symbols = 64;
  rmt_cfg.flags.with_dma = false;

  led_strip_handle_t strip = nullptr;
  if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip) != ESP_OK || strip == nullptr) {
    ESP_LOGE(kTag, "Failed to initialize LED strip");
    vTaskDelete(nullptr);
    return;
  }

  AppConfig cfg = {};
  uint32_t cfg_version = 0;
  ConfigStore_GetCopy(ctx->config_store, &cfg, &cfg_version);
  SystemState_SetConfigVersionApplied(ctx->state_store, 0, 0, cfg_version);

  uint32_t frame = 0;

  for (;;) {
    AppConfig latest_cfg = {};
    uint32_t latest_version = 0;
    if (ConfigStore_GetCopy(ctx->config_store, &latest_cfg, &latest_version) &&
        latest_version != cfg_version) {
      cfg = latest_cfg;
      cfg_version = latest_version;
      SystemState_SetConfigVersionApplied(ctx->state_store, 0, 0, cfg_version);
      ESP_LOGI(kTag, "Config reloaded in LED task (version=%lu)",
               static_cast<unsigned long>(cfg_version));
    }

    SystemState state = {};
    SystemState_GetCopy(ctx->state_store, &state);

    uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
    bool recent_catch = state.fish_caught_latched &&
                        ((now_ms - state.last_catch_tick_ms) <= kCatchVisualMs);

    if (state.fish_caught_latched && !recent_catch) {
      SystemState_ClearCatchLatch(ctx->state_store);
    }

    LedPattern pattern = LedPattern::kBreath;
    Rgb color = {10, 120, 30};

    if (recent_catch) {
      pattern = LedPattern::kChase;
      color = {255, 100, 0};
    }

    RenderPattern(strip, pattern, color, cfg.led_brightness, frame);

    frame += 1;
    vTaskDelay(pdMS_TO_TICKS(kFrameDelayMs));
  }
}
}  // namespace

bool LedTask_Start(SharedConfigStore* config_store, SharedSystemState* state_store) {
  if (config_store == nullptr || state_store == nullptr) {
    return false;
  }

  static LedTaskContext ctx = {};
  ctx.config_store = config_store;
  ctx.state_store = state_store;

  BaseType_t ok = xTaskCreatePinnedToCore(LedTaskEntry, "LEDTask", 5120, &ctx, 3, nullptr, 1);
  return ok == pdPASS;
}
