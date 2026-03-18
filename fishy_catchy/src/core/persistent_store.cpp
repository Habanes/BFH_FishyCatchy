#include "core/persistent_store.hpp"

#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace {
constexpr TickType_t kStoreLockTimeoutTicks = pdMS_TO_TICKS(50);
constexpr const char* kNvsNamespace = "fishycfg";
constexpr const char* kNvsConfigKey = "syscfg";

esp_err_t ensure_nvs_ready() {
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		(void)nvs_flash_erase();
		err = nvs_flash_init();
	}
	return err;
}

bool read_config_from_nvs(fishy_catchy::core::SystemConfig* out_config) {
	if (out_config == nullptr) {
		return false;
	}

	nvs_handle_t handle = 0;
	if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
		return false;
	}

	fishy_catchy::core::SystemConfig stored_config{};
	size_t blob_size = sizeof(stored_config);
	const esp_err_t get_err = nvs_get_blob(handle, kNvsConfigKey, &stored_config, &blob_size);
	nvs_close(handle);

	if (get_err != ESP_OK || blob_size != sizeof(stored_config)) {
		return false;
	}

	*out_config = stored_config;
	return true;
}

bool write_config_to_nvs(const fishy_catchy::core::SystemConfig& config) {
	nvs_handle_t handle = 0;
	if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
		return false;
	}

	const esp_err_t set_err = nvs_set_blob(handle, kNvsConfigKey, &config, sizeof(config));
	const esp_err_t commit_err = (set_err == ESP_OK) ? nvs_commit(handle) : set_err;
	nvs_close(handle);

	return commit_err == ESP_OK;
}
}

namespace fishy_catchy::core {

PersistentStore::PersistentStore() {
	config_ = sanitize_system_config(make_default_system_config());
}

PersistentStore::~PersistentStore() {
	if (mutex_ != nullptr) {
		vSemaphoreDelete(mutex_);
		mutex_ = nullptr;
	}
}

bool PersistentStore::init() {
	if (mutex_ != nullptr) {
		return true;
	}

	mutex_ = xSemaphoreCreateMutex();
	if (mutex_ == nullptr) {
		return false;
	}

	nvs_ready_ = (ensure_nvs_ready() == ESP_OK);
	if (nvs_ready_) {
		SystemConfig stored_config{};
		if (read_config_from_nvs(&stored_config)) {
			config_ = sanitize_system_config(stored_config);
		} else {
			(void)write_config_to_nvs(config_);
		}
	}

	return true;
}

bool PersistentStore::is_initialized() const {
	return mutex_ != nullptr;
}

bool PersistentStore::load(SystemConfig* out_config) const {
	if (out_config == nullptr) {
		return false;
	}

	if (!lock()) {
		return false;
	}

	*out_config = config_;
	unlock();
	return true;
}

bool PersistentStore::save(const SystemConfig& config) {
	if (!lock()) {
		return false;
	}

	config_ = sanitize_system_config(config);
	if (nvs_ready_) {
		(void)write_config_to_nvs(config_);
	}
	unlock();
	return true;
}

bool PersistentStore::reset_to_defaults() {
	return save(make_default_system_config());
}

bool PersistentStore::lock() const {
	if (mutex_ == nullptr) {
		return false;
	}

	return xSemaphoreTake(mutex_, kStoreLockTimeoutTicks) == pdTRUE;
}

void PersistentStore::unlock() const {
	if (mutex_ == nullptr) {
		return;
	}

	(void)xSemaphoreGive(mutex_);
}

}  // namespace fishy_catchy::core
