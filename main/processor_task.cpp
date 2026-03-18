#include "processor_task.hpp"

#include <stdlib.h>

#include "esp_log.h"

namespace {
constexpr char kTag[] = "ProcessorTask";

struct ProcessorTaskContext {
  SharedConfigStore* config_store;
  SharedSystemState* state_store;
  QueueHandle_t sample_queue;
};

struct DeltaRingBuffer {
  uint32_t values[kMaxWindowSamples];
  uint16_t head;
  uint16_t count;
};

void PushDelta(DeltaRingBuffer* buffer, uint32_t value) {
  buffer->values[buffer->head] = value;
  buffer->head = (buffer->head + 1) % kMaxWindowSamples;
  if (buffer->count < kMaxWindowSamples) {
    buffer->count += 1;
  }
}

uint32_t ReadNewest(const DeltaRingBuffer& buffer, uint16_t offset) {
  if (offset >= buffer.count) {
    return 0;
  }
  int32_t index = static_cast<int32_t>(buffer.head) - 1 - static_cast<int32_t>(offset);
  while (index < 0) {
    index += kMaxWindowSamples;
  }
  return buffer.values[index];
}

bool EvalSingleSpike(const AppConfig& cfg, uint32_t delta) {
  return delta >= cfg.single_spike_threshold;
}

bool EvalDenseSpikes(const AppConfig& cfg, const DeltaRingBuffer& buffer) {
  uint16_t samples = cfg.dense_window_samples;
  if (samples > buffer.count) {
    samples = buffer.count;
  }

  uint16_t hits = 0;
  for (uint16_t i = 0; i < samples; ++i) {
    if (ReadNewest(buffer, i) >= cfg.dense_spike_threshold) {
      hits += 1;
      if (hits >= cfg.dense_required_hits) {
        return true;
      }
    }
  }

  return false;
}

bool EvalCumulative(const AppConfig& cfg, const DeltaRingBuffer& buffer) {
  uint16_t samples = cfg.cumulative_window_samples;
  if (samples > buffer.count) {
    samples = buffer.count;
  }

  uint64_t sum = 0;
  for (uint16_t i = 0; i < samples; ++i) {
    sum += ReadNewest(buffer, i);
    if (sum >= cfg.cumulative_threshold) {
      return true;
    }
  }

  return false;
}

void ProcessorTaskEntry(void* parameter) {
  ProcessorTaskContext* ctx = static_cast<ProcessorTaskContext*>(parameter);

  AppConfig cfg = {};
  uint32_t cfg_version = 0;
  ConfigStore_GetCopy(ctx->config_store, &cfg, &cfg_version);
  SystemState_SetConfigVersionApplied(ctx->state_store, 0, cfg_version, 0);

  DeltaRingBuffer ring = {};
  bool has_prev = false;
  int64_t prev_mag_sq = 0;
  uint32_t last_trigger_ms = 0;

  for (;;) {
    SensorSample sample = {};
    if (xQueueReceive(ctx->sample_queue, &sample, portMAX_DELAY) != pdTRUE) {
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

    int64_t x = sample.x;
    int64_t y = sample.y;
    int64_t z = sample.z;
    int64_t mag_sq = (x * x) + (y * y) + (z * z);

    if (!has_prev) {
      prev_mag_sq = mag_sq;
      has_prev = true;
      continue;
    }

    int64_t delta_signed = mag_sq - prev_mag_sq;
    prev_mag_sq = mag_sq;
    uint64_t delta_abs = static_cast<uint64_t>(llabs(delta_signed));
    uint32_t delta = (delta_abs > UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(delta_abs);

    PushDelta(&ring, delta);

    bool detected = false;
    DetectionAlgorithm algo = static_cast<DetectionAlgorithm>(cfg.algorithm);
    switch (algo) {
      case DetectionAlgorithm::kSingleSpike:
        detected = EvalSingleSpike(cfg, delta);
        break;
      case DetectionAlgorithm::kDenseSpikes:
        detected = EvalDenseSpikes(cfg, ring);
        break;
      case DetectionAlgorithm::kCumulative:
      default:
        detected = EvalCumulative(cfg, ring);
        break;
    }

    if (detected) {
      uint32_t now_ms = sample.tick_ms;
      uint32_t elapsed = now_ms - last_trigger_ms;
      if (last_trigger_ms == 0 || elapsed >= cfg.catch_cooldown_ms) {
        last_trigger_ms = now_ms;
        SystemState_RegisterCatch(ctx->state_store, now_ms);
      }
    }
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
