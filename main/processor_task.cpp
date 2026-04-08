#include "processor_task.hpp"

#include <math.h>

#include "esp_log.h"

namespace {
constexpr char kTag[] = "ProcessorTask";

struct ProcessorTaskContext {
  SharedConfigStore* config_store;
  SharedSystemState* state_store;
  QueueHandle_t sample_queue;
};

struct FloatRingBuffer {
  float values[kMaxWindowSamples];
  uint16_t head;
  uint16_t count;
};

void PushValue(FloatRingBuffer* buffer, float value) {
  buffer->values[buffer->head] = value;
  buffer->head = (buffer->head + 1) % kMaxWindowSamples;
  if (buffer->count < kMaxWindowSamples) {
    buffer->count += 1;
  }
}

float ReadNewest(const FloatRingBuffer& buffer, uint16_t offset) {
  if (offset >= buffer.count) {
    return 0.0f;
  }
  int32_t index = static_cast<int32_t>(buffer.head) - 1 - static_cast<int32_t>(offset);
  while (index < 0) {
    index += kMaxWindowSamples;
  }
  return buffer.values[index];
}

float ComputeAbsAxisSum(const SensorSample& sample) {
  return fabsf(sample.ax) + fabsf(sample.ay) + fabsf(sample.az);
}

bool EvalSingleSpike(const AppConfig& cfg, float abs_axis_sum) {
  return abs_axis_sum > cfg.single_spike_threshold;
}

float ComputeCumulativeAverage(const AppConfig& cfg, const FloatRingBuffer& buffer) {
  constexpr uint16_t kSensorPeriodMs = 2;
  uint16_t required_samples = cfg.cumulative_window_ms / kSensorPeriodMs;
  uint16_t samples = (required_samples > buffer.count) ? buffer.count : required_samples;
  
  if (samples == 0) {
    return 0.0f;
  }

  float sum = 0.0f;
  for (uint16_t i = 0; i < samples; ++i) {
    sum += ReadNewest(buffer, i);
  }
  return sum / static_cast<float>(samples);
}

void ProcessorTaskEntry(void* parameter) {
  ProcessorTaskContext* ctx = static_cast<ProcessorTaskContext*>(parameter);

  AppConfig cfg = {};
  uint32_t cfg_version = 0;
  ConfigStore_GetCopy(ctx->config_store, &cfg, &cfg_version);
  SystemState_SetConfigVersionApplied(ctx->state_store, 0, cfg_version, 0);

  FloatRingBuffer ring = {};
  uint32_t last_trigger_ms = 0;

  for (;;) {
    SensorSample sample = {};
    if (xQueueReceive(ctx->sample_queue, &sample, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    if (!sample.accel_valid) {
      continue;
    }

    AppConfig latest_cfg = {};
    uint32_t latest_version = 0;
    if (ConfigStore_GetCopy(ctx->config_store, &latest_cfg, &latest_version) &&
        latest_version != cfg_version) {
      cfg = latest_cfg;
      cfg_version = latest_version;
      SystemState_SetConfigVersionApplied(ctx->state_store, 0, cfg_version, 0);
      ESP_LOGI(kTag, "Config reloaded in processor (version=%lu)",
               static_cast<unsigned long>(cfg_version));
    }

    float abs_axis_sum = ComputeAbsAxisSum(sample);
    PushValue(&ring, abs_axis_sum);

    float cumulative_avg = ComputeCumulativeAverage(cfg, ring);

    bool detected = false;
    DetectionAlgorithm algo = static_cast<DetectionAlgorithm>(cfg.algorithm);
    if (algo == DetectionAlgorithm::kSingleSpike) {
      detected = EvalSingleSpike(cfg, abs_axis_sum);
    } else {
      detected = cumulative_avg >= cfg.cumulative_threshold;
    }

    SystemState_UpdateProcessingMetrics(
        ctx->state_store, abs_axis_sum, cumulative_avg, detected,
        static_cast<uint8_t>(algo));

    if (!detected) {
      continue;
    }

    uint32_t now_ms = sample.tick_ms;
    uint32_t elapsed = now_ms - last_trigger_ms;
    if (last_trigger_ms != 0 && elapsed < cfg.catch_cooldown_ms) {
      continue;
    }

    last_trigger_ms = now_ms;
    SystemState_RegisterCatch(ctx->state_store, now_ms);
  }
}
}  // namespace

bool ProcessorTask_Start(SharedConfigStore* config_store,
                         SharedSystemState* state_store,
                         QueueHandle_t sample_queue) {
  if (config_store == nullptr || state_store == nullptr || sample_queue == nullptr) {
    return false;
  }

  static ProcessorTaskContext ctx = {};
  ctx.config_store = config_store;
  ctx.state_store = state_store;
  ctx.sample_queue = sample_queue;

  BaseType_t ok = xTaskCreatePinnedToCore(ProcessorTaskEntry, "ProcessorTask", 6144, &ctx,
                                          7, nullptr, 1);
  return ok == pdPASS;
}
