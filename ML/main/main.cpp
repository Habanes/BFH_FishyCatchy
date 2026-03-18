#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ml_model.h"

extern "C" void app_main(void) {
    if (!ml_init()) {
        std::printf("ML init failed\n");
        return;
    }

    struct Example {
        float length_mm;
        float outer_diameter_mm;
        float weight_g;
        const char* expected;
    };

    // Example “thing recognition” based on three measured values.
    // In a real project, replace these constants with sensor readings.
    Example examples[] = {
        {45.0f, 5.0f, 12.0f, "screw"},
        {8.0f, 13.0f, 2.8f, "nut"},
        {2.0f, 18.0f, 1.5f, "washer"},
    };

    while (true) {
        for (const auto& ex : examples) {
            DetectionResult result{};
            if (ml_predict(ex.length_mm, ex.outer_diameter_mm, ex.weight_g, &result)) {
                std::printf(
                    "input={len=%.1fmm, dia=%.1fmm, weight=%.1fg} -> predicted=%s (expected=%s) | "
                    "scores=[screw=%.3f, nut=%.3f, washer=%.3f]\n",
                    ex.length_mm,
                    ex.outer_diameter_mm,
                    ex.weight_g,
                    ml_label_from_index(result.class_index),
                    ex.expected,
                    result.scores[0],
                    result.scores[1],
                    result.scores[2]);
            } else {
                std::printf("prediction failed\n");
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}
