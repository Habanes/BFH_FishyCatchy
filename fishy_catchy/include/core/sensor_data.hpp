#pragma once

#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace fishy_catchy::core {

struct SensorSample {
	int16_t x = 0;
	int16_t y = 0;
	int16_t z = 0;
};

class SensorSampleQueue {
public:
	SensorSampleQueue() = default;
	~SensorSampleQueue();

	SensorSampleQueue(const SensorSampleQueue&) = delete;
	SensorSampleQueue& operator=(const SensorSampleQueue&) = delete;

	bool init(std::size_t capacity);
	void deinit();
	bool is_initialized() const;

	bool push(const SensorSample& sample, TickType_t timeout_ticks);
	bool pop(SensorSample* out_sample, TickType_t timeout_ticks);

	std::size_t size() const;
	std::size_t capacity() const;

private:
	QueueHandle_t queue_ = nullptr;
	std::size_t capacity_ = 0;
};

}  // namespace fishy_catchy::core
