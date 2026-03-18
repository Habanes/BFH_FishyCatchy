#pragma once

#include "app_types.hpp"

#include "esp_err.h"

class ConfigStore {
public:
    static esp_err_t init();
    static esp_err_t load(Config& out_config);
    static esp_err_t save(const Config& config);
    static Config defaults();

private:
    static esp_err_t validate(const Config& config);
};
