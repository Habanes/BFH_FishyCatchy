#include "core/system_state.hpp"

namespace {
constexpr TickType_t kStateLockTimeoutTicks = pdMS_TO_TICKS(25);
}

namespace fishy_catchy::core {

SystemState::SystemState() {
    mutex_ = xSemaphoreCreateMutex();
}

SystemState::~SystemState() {
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

AppStatus SystemState::get_app_status() const {
    if (!lock()) {
        return AppStatus::Error;
    }

    const AppStatus status = app_status_;
    unlock();
    return status;
}

WifiStatus SystemState::get_wifi_status() const {
    if (!lock()) {
        return WifiStatus::Error;
    }

    const WifiStatus status = wifi_status_;
    unlock();
    return status;
}

FishCaughtStatus SystemState::get_fish_caught_status() const {
    if (!lock()) {
        return FishCaughtStatus::Error;
    }

    const FishCaughtStatus status = fish_caught_status_;
    unlock();
    return status;
}

void SystemState::set_app_status(AppStatus status) {
    if (!lock()) {
        return;
    }

    app_status_ = status;
    unlock();
}

void SystemState::set_wifi_status(WifiStatus status) {
    if (!lock()) {
        return;
    }

    wifi_status_ = status;
    unlock();
}

void SystemState::set_fish_caught_status(FishCaughtStatus status) {
    if (!lock()) {
        return;
    }

    fish_caught_status_ = status;
    unlock();
}

bool SystemState::lock() const {
    if (mutex_ == nullptr) {
        return false;
    }

    return xSemaphoreTake(mutex_, kStateLockTimeoutTicks) == pdTRUE;
}

void SystemState::unlock() const {
    if (mutex_ == nullptr) {
        return;
    }

    (void)xSemaphoreGive(mutex_);
}

}  // namespace fishy_catchy::core
