#pragma once

#include <array>
#include <stdint.h>

#include "app_types.hpp"

class StrikeDetector {
public:
    static constexpr size_t kMaxWindow = 128;

    void configure(const Config& config);
    bool push_sample(const ImuSample& sample);

private:
    uint32_t magnitude_sq(const ImuSample& sample) const;

    DetectionAlgorithm algorithm_ = DetectionAlgorithm::DensityThreshold;
    uint32_t bite_threshold_ = 120000;
    uint16_t density_window_ = 20;
    uint16_t density_hits_ = 5;
    uint32_t cumulative_threshold_ = 600000;

    std::array<uint32_t, kMaxWindow> deltas_{};
    size_t write_idx_ = 0;
    size_t count_ = 0;
    uint32_t previous_magnitude_ = 0;
    bool has_previous_ = false;
};
