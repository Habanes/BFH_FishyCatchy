#include "sensor_task.hpp"

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"

namespace {
constexpr char kTag[] = "SensorTask";
constexpr i2c_port_t kI2cPort = I2C_NUM_0;
constexpr uint8_t kMc6470Address = 0x4C;
constexpr uint8_t kRegMode = 0x07;
constexpr uint8_t kRegXoutLow = 0x0D;

struct SensorTaskContext {
  SharedConfigStore* config_store;
  SharedSystemState* state_store;
  QueueHandle_t sample_queue;
};

bool InitMotorPins() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = (1ULL << BoardPins::kMotorWake) |
                     (1ULL << BoardPins::kMotorEnable) |
                     (1ULL << BoardPins::kMotorReverse);
  cfg.mode = GPIO_MODE_OUTPUT;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  esp_err_t err = gpio_config(&cfg);
  if (err != ESP_OK) {
    return false;
  }

  gpio_set_level(static_cast<gpio_num_t>(BoardPins::kMotorWake), 0);
  gpio_set_level(static_cast<gpio_num_t>(BoardPins::kMotorEnable), 0);
  gpio_set_level(static_cast<gpio_num_t>(BoardPins::kMotorReverse), 0);
  return true;
}

bool InitI2c() {
  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = static_cast<gpio_num_t>(BoardPins::kI2cSda);
  conf.scl_io_num = static_cast<gpio_num_t>(BoardPins::kI2cScl);
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = 400000;

  esp_err_t err = i2c_param_config(kI2cPort, &conf);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "i2c_param_config failed: %s", esp_err_to_name(err));
    return false;
  }

  err = i2c_driver_install(kI2cPort, conf.mode, 0, 0, 0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "i2c_driver_install failed: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

esp_err_t WriteReg(uint8_t reg, uint8_t value) {
  uint8_t bytes[2] = {reg, value};
  return i2c_master_write_to_device(kI2cPort, kMc6470Address, bytes, sizeof(bytes),
                                    pdMS_TO_TICKS(50));
}

esp_err_t ReadRegs(uint8_t start_reg, uint8_t* out, size_t len) {
  return i2c_master_write_read_device(kI2cPort, kMc6470Address, &start_reg, 1, out,
                                      len, pdMS_TO_TICKS(50));
}

bool InitMc6470() {
  // Basic wake-up sequence. Register map should be verified for final hardware tune.
  esp_err_t err = WriteReg(kRegMode, 0x00);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "MC6470 standby write failed: %s", esp_err_to_name(err));
    return false;
  }
  err = WriteReg(kRegMode, 0x01);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "MC6470 wake write failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool ReadSample(SensorSample* out_sample) {
  uint8_t raw[6] = {};
  esp_err_t err = ReadRegs(kRegXoutLow, raw, sizeof(raw));
  if (err != ESP_OK) {
    return false;
  }

  out_sample->x = static_cast<int16_t>((raw[1] << 8) | raw[0]);
  out_sample->y = static_cast<int16_t>((raw[3] << 8) | raw[2]);
  out_sample->z = static_cast<int16_t>((raw[5] << 8) | raw[4]);
  out_sample->tick_ms = pdTICKS_TO_MS(xTaskGetTickCount());
  return true;
}

void SensorTaskEntry(void* parameter) {
  SensorTaskContext* ctx = static_cast<SensorTaskContext*>(parameter);

  InitMotorPins();
  bool i2c_ready = InitI2c() && InitMc6470();

  AppConfig cfg = {};
  uint32_t cfg_version = 0;
  ConfigStore_GetCopy(ctx->config_store, &cfg, &cfg_version);
  SystemState_SetConfigVersionApplied(ctx->state_store, cfg_version, 0, 0);

  TickType_t last_wake = xTaskGetTickCount();
  uint32_t local_period_ms = cfg.sensor_period_ms;

  for (;;) {
    AppConfig latest_cfg = {};
    uint32_t latest_version = 0;
    if (ConfigStore_GetCopy(ctx->config_store, &latest_cfg, &latest_version) &&
        latest_version != cfg_version) {
      cfg = latest_cfg;
      cfg_version = latest_version;
      local_period_ms = cfg.sensor_period_ms;
      SystemState_SetConfigVersionApplied(ctx->state_store, cfg_version, 0, 0);
    }

    if (i2c_ready) {
      SensorSample sample = {};
      if (ReadSample(&sample)) {
        if (xQueueSend(ctx->sample_queue, &sample, 0) != pdTRUE) {
          SensorSample dropped = {};
          xQueueReceive(ctx->sample_queue, &dropped, 0);
          xQueueSend(ctx->sample_queue, &sample, 0);
        }
      }
    }

    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(local_period_ms));
  }
}
}  // namespace

bool SensorTask_Start(SharedConfigStore* config_store,
                      SharedSystemState* state_store,
                      QueueHandle_t sample_queue) {
  if (config_store == nullptr || state_store == nullptr || sample_queue == nullptr) {
    return false;
  }

  static SensorTaskContext ctx = {};
  ctx.config_store = config_store;
  ctx.state_store = state_store;
  ctx.sample_queue = sample_queue;

  BaseType_t ok = xTaskCreatePinnedToCore(SensorTaskEntry, "SensorTask", 4096, &ctx, 6,
                                          nullptr, 1);
  return ok == pdPASS;
}
