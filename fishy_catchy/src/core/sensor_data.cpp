#include "core/sensor_data.hpp"

namespace fishy_catchy::core {

SensorSampleQueue::~SensorSampleQueue() {
	deinit();
}

bool SensorSampleQueue::init(std::size_t capacity) {
	if (queue_ != nullptr) {
		return true;
	}
	if (capacity == 0) {
		return false;
	}

	queue_ = xQueueCreate(static_cast<UBaseType_t>(capacity), sizeof(SensorSample));
	if (queue_ == nullptr) {
		return false;
	}

	capacity_ = capacity;
	return true;
}

void SensorSampleQueue::deinit() {
	if (queue_ == nullptr) {
		return;
	}

	vQueueDelete(queue_);
	queue_ = nullptr;
	capacity_ = 0;
}

bool SensorSampleQueue::is_initialized() const {
	return queue_ != nullptr;
}

bool SensorSampleQueue::push(const SensorSample& sample, TickType_t timeout_ticks) {
	if (queue_ == nullptr) {
		return false;
	}

	return xQueueSend(queue_, &sample, timeout_ticks) == pdTRUE;
}

bool SensorSampleQueue::pop(SensorSample* out_sample, TickType_t timeout_ticks) {
	if (queue_ == nullptr || out_sample == nullptr) {
		return false;
	}

	return xQueueReceive(queue_, out_sample, timeout_ticks) == pdTRUE;
}

std::size_t SensorSampleQueue::size() const {
	if (queue_ == nullptr) {
		return 0;
	}

	return static_cast<std::size_t>(uxQueueMessagesWaiting(queue_));
}

std::size_t SensorSampleQueue::capacity() const {
	return capacity_;
}

}  // namespace fishy_catchy::core
