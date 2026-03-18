#include "strike_detector.hpp"

#include <algorithm>

void StrikeDetector::configure(const Config& config) {
    algorithm_ = config.algorithm;
    bite_threshold_ = config.bite_threshold;
    density_window_ = std::min<uint16_t>(config.density_window_samples, kMaxWindow);
    density_hits_ = config.density_threshold_hits;
    cumulative_threshold_ = config.cumulative_threshold;

    write_idx_ = 0;
    count_ = 0;
    has_previous_ = false;
    previous_magnitude_ = 0;
    deltas_.fill(0);
}

uint32_t StrikeDetector::magnitude_sq(const ImuSample& sample) const {
    const int32_t x = sample.x;
    const int32_t y = sample.y;
    const int32_t z = sample.z;
    return static_cast<uint32_t>(x * x + y * y + z * z);
}

bool StrikeDetector::push_sample(const ImuSample& sample) {
    const uint32_t current = magnitude_sq(sample);

    if (!has_previous_) {
        previous_magnitude_ = current;
        has_previous_ = true;
        return false;
    }

    uint32_t delta = (current > previous_magnitude_) ? (current - previous_magnitude_)
                                                      : (previous_magnitude_ - current);
    previous_magnitude_ = current;

    deltas_[write_idx_] = delta;
    write_idx_ = (write_idx_ + 1) % density_window_;
    if (count_ < density_window_) {
        ++count_;
    }

    if (algorithm_ == DetectionAlgorithm::SingleThreshold) {
        return delta > bite_threshold_;
    }

    if (count_ < density_window_) {
        return false;
    }

    if (algorithm_ == DetectionAlgorithm::DensityThreshold) {
        uint16_t hits = 0;
        for (size_t i = 0; i < density_window_; ++i) {
            if (deltas_[i] > bite_threshold_) {
                ++hits;
            }
        }
        return hits >= density_hits_;
    }

    uint64_t sum = 0;
    for (size_t i = 0; i < density_window_; ++i) {
        sum += deltas_[i];
    }
    return sum > cumulative_threshold_;
}
