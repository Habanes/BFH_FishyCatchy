#include "sensor_task.hpp"

#include <math.h>
#include <stdint.h>

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"

namespace {
constexpr char kTag[] = "SensorTask";
constexpr i2c_port_t kI2cPort = I2C_NUM_0;
constexpr uint8_t kAccelAddresses[] = {0x4C, 0x6C};
constexpr uint8_t kMagAddresses[] = {0x0C, 0x0E, 0x1E};
constexpr uint8_t kAccelModeReg = 0x07;
constexpr uint8_t kAccelDataReg = 0x0D;
constexpr uint8_t kMagModeReg = 0x31;
constexpr uint8_t kMagDataReg = 0x11;
constexpr float kAccelLsbPerG = 1024.0f;
constexpr float kAccelScaleCorrection = 60.0f;
constexpr float kMagUtPerLsb = 0.15f;

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
  if (gpio_config(&cfg) != ESP_OK) {
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

esp_err_t WriteReg(uint8_t address, uint8_t reg, uint8_t value) {
  uint8_t bytes[2] = {reg, value};
  return i2c_master_write_to_device(kI2cPort, address, bytes, sizeof(bytes),
                                    pdMS_TO_TICKS(30));
}

esp_err_t ReadRegs(uint8_t address, uint8_t reg, uint8_t* out, size_t len) {
  return i2c_master_write_read_device(kI2cPort, address, &reg, 1, out, len,
                                      pdMS_TO_TICKS(30));
}

bool ProbeAccelAddress(uint8_t address) {
  uint8_t buf[1] = {};
  return ReadRegs(address, kAccelDataReg, buf, sizeof(buf)) == ESP_OK;
}

bool InitAccelerometer(uint8_t* out_address) {
  if (out_address == nullptr) {
    return false;
  }

  for (uint8_t address : kAccelAddresses) {
    if (!ProbeAccelAddress(address)) {
      continue;
    }

    esp_err_t standby = WriteReg(address, kAccelModeReg, 0x00);
    esp_err_t wake = WriteReg(address, kAccelModeReg, 0x01);
    if (standby == ESP_OK && wake == ESP_OK) {
      *out_address = address;
      ESP_LOGI(kTag, "Accelerometer detected at 0x%02X", address);
      return true;
    }
  }

  ESP_LOGW(kTag, "No accelerometer detected");
  return false;
}

bool InitMagnetometer(uint8_t* out_address) {
  if (out_address == nullptr) {
    return false;
  }

  for (uint8_t address : kMagAddresses) {
    uint8_t probe = 0;
    if (ReadRegs(address, kMagDataReg, &probe, 1) != ESP_OK) {
      continue;
    }

    WriteReg(address, kMagModeReg, 0x08);
    *out_address = address;
    ESP_LOGI(kTag, "Magnetometer detected at 0x%02X", address);
    return true;
  }

  ESP_LOGW(kTag, "No magnetometer detected");
  return false;
}

inline bool IsDegenerateAxis(int16_t x, int16_t y, int16_t z) {
  return (x == 0 && y == 0 && z == 0) ||
         (x == y && y == z && x != 0);
}

void DecodeAccelPrimary(const uint8_t raw[6], int16_t* x, int16_t* y, int16_t* z) {
  *x = static_cast<int16_t>((raw[1] << 8) | raw[0]);
  *y = static_cast<int16_t>((raw[3] << 8) | raw[2]);
  *z = static_cast<int16_t>((raw[5] << 8) | raw[4]);
}

void DecodeAccelSwapped(const uint8_t raw[6], int16_t* x, int16_t* y, int16_t* z) {
  *x = static_cast<int16_t>((raw[0] << 8) | raw[1]);
  *y = static_cast<int16_t>((raw[2] << 8) | raw[3]);
  *z = static_cast<int16_t>((raw[4] << 8) | raw[5]);
}

bool ReadAccelInG(uint8_t accel_address, float* out_ax, float* out_ay, float* out_az) {
  if (out_ax == nullptr || out_ay == nullptr || out_az == nullptr) {
    return false;
  }

  uint8_t raw[6] = {};
  if (ReadRegs(accel_address, kAccelDataReg, raw, sizeof(raw)) != ESP_OK) {
    return false;
  }

  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
  DecodeAccelPrimary(raw, &x, &y, &z);

  if (IsDegenerateAxis(x, y, z)) {
    DecodeAccelSwapped(raw, &x, &y, &z);
  }

  *out_ax = (static_cast<float>(x) / kAccelLsbPerG) * kAccelScaleCorrection;
  *out_ay = (static_cast<float>(y) / kAccelLsbPerG) * kAccelScaleCorrection;
  *out_az = (static_cast<float>(z) / kAccelLsbPerG) * kAccelScaleCorrection;
  return true;
}

bool ReadMagInUt(uint8_t mag_address, float* out_mx, float* out_my, float* out_mz) {
  if (out_mx == nullptr || out_my == nullptr || out_mz == nullptr) {
    return false;
  }

  uint8_t raw[8] = {};
  if (ReadRegs(mag_address, kMagDataReg, raw, sizeof(raw)) != ESP_OK) {
    return false;
  }

  int16_t x = static_cast<int16_t>((raw[1] << 8) | raw[0]);
  int16_t y = static_cast<int16_t>((raw[3] << 8) | raw[2]);
  int16_t z = static_cast<int16_t>((raw[5] << 8) | raw[4]);

  if (IsDegenerateAxis(x, y, z)) {
    x = static_cast<int16_t>((raw[0] << 8) | raw[1]);
    y = static_cast<int16_t>((raw[2] << 8) | raw[3]);
    z = static_cast<int16_t>((raw[4] << 8) | raw[5]);
    if (IsDegenerateAxis(x, y, z)) {
      return false;
    }
  }

  *out_mx = static_cast<float>(x) * kMagUtPerLsb;
  *out_my = static_cast<float>(y) * kMagUtPerLsb;
  *out_mz = static_cast<float>(z) * kMagUtPerLsb;
  return true;
}

void PushLatestSample(SensorTaskContext* ctx, const SensorSample& sample) {
  SystemState_UpdateLatestSensor(ctx->state_store, &sample);

  if (!sample.accel_valid) {
    return;
  }

  if (xQueueSend(ctx->sample_queue, &sample, 0) == pdTRUE) {
    return;
  }

  SensorSample dropped = {};
  xQueueReceive(ctx->sample_queue, &dropped, 0);
  xQueueSend(ctx->sample_queue, &sample, 0);
}

void SensorTaskEntry(void* parameter) {
  SensorTaskContext* ctx = static_cast<SensorTaskContext*>(parameter);

  InitMotorPins();
  bool i2c_ready = InitI2c();

  uint8_t accel_address = 0;
  uint8_t mag_address = 0;
  bool accel_ready = i2c_ready && InitAccelerometer(&accel_address);
  bool mag_ready = i2c_ready && InitMagnetometer(&mag_address);

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

    SensorSample sample = {};
    sample.tick_ms = pdTICKS_TO_MS(xTaskGetTickCount());

    if (accel_ready) {
      sample.accel_valid = ReadAccelInG(accel_address, &sample.ax, &sample.ay, &sample.az);
      if (!sample.accel_valid) {
        accel_ready = InitAccelerometer(&accel_address);
      }
    }

    if (mag_ready) {
      sample.mag_valid = ReadMagInUt(mag_address, &sample.mx, &sample.my, &sample.mz);
      if (!sample.mag_valid) {
        mag_ready = InitMagnetometer(&mag_address);
      }
    }

    PushLatestSample(ctx, sample);

    TickType_t period_ticks = pdMS_TO_TICKS(local_period_ms);
    if (period_ticks < 1) {
      vTaskDelay(1);
      last_wake = xTaskGetTickCount();
    } else {
      vTaskDelayUntil(&last_wake, period_ticks);
    }
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

  BaseType_t ok = xTaskCreatePinnedToCore(SensorTaskEntry, "SensorTask", 6144, &ctx, 6,
                                          nullptr, 1);
  return ok == pdPASS;
}
