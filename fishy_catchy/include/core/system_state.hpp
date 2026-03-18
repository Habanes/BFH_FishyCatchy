#pragma once

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace fishy_catchy::core {

enum class AppStatus : uint8_t {
    Startup,
    Running,
    Error
};

enum class WifiStatus : uint8_t {
    Off,
    Searching,
    Connected,
    Error
};

enum class FishCaughtStatus : uint8_t {
    No,
    Yes,
    Error
};

class SystemState {
public:
    SystemState();
    ~SystemState();

    SystemState(const SystemState&) = delete;
    SystemState& operator=(const SystemState&) = delete;

    AppStatus get_app_status() const;
    WifiStatus get_wifi_status() const;
    FishCaughtStatus get_fish_caught_status() const;

    void set_app_status(AppStatus status);
    void set_wifi_status(WifiStatus status);
    void set_fish_caught_status(FishCaughtStatus status);

private:
    bool lock() const;
    void unlock() const;

    mutable SemaphoreHandle_t mutex_ = nullptr;
    AppStatus app_status_ = AppStatus::Startup;
    WifiStatus wifi_status_ = WifiStatus::Off;
    FishCaughtStatus fish_caught_status_ = FishCaughtStatus::No;
};

}  // namespace fishy_catchy::core
