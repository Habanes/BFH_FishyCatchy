#pragma once

#include <cstdint>

struct DetectionResult {
    int class_index;
    float scores[3];
};

bool ml_init();
bool ml_predict(float length_mm, float outer_diameter_mm, float weight_g, DetectionResult* out_result);
const char* ml_label_from_index(int idx);
