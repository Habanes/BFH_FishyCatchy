#pragma once

#include "core/system_config.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace fishy_catchy::core {

class PersistentStore {
public:
	PersistentStore();
	~PersistentStore();

	PersistentStore(const PersistentStore&) = delete;
	PersistentStore& operator=(const PersistentStore&) = delete;

	bool init();
	bool is_initialized() const;

	bool load(SystemConfig* out_config) const;
	bool save(const SystemConfig& config);
	bool reset_to_defaults();

private:
	bool lock() const;
	void unlock() const;

	mutable SemaphoreHandle_t mutex_ = nullptr;
	bool nvs_ready_ = false;
	SystemConfig config_{};
};

}  // namespace fishy_catchy::core
