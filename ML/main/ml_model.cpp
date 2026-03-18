#include "ml_model.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

namespace {
constexpr int kNumClasses = 3;
constexpr int kTensorArenaSize = 12 * 1024;
uint8_t tensor_arena[kTensorArenaSize];

constexpr float kFeatureMin[3] = {1.0f, 3.0f, 0.1f};
constexpr float kFeatureMax[3] = {80.0f, 30.0f, 40.0f};
constexpr const char* kLabels[kNumClasses] = {"screw", "nut", "washer"};

const tflite::Model* g_model = nullptr;
tflite::MicroInterpreter* g_interpreter = nullptr;
TfLiteTensor* g_input = nullptr;
TfLiteTensor* g_output = nullptr;

float normalize_value(float x, float lo, float hi) {
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    return (x - lo) / (hi - lo);
}

int8_t quantize_to_int8(float value, float scale, int zero_point) {
    const int32_t q = static_cast<int32_t>(std::lround(value / scale)) + zero_point;
    if (q < -128) return -128;
    if (q > 127) return 127;
    return static_cast<int8_t>(q);
}

float dequantize_from_int8(int8_t value, float scale, int zero_point) {
    return (static_cast<int>(value) - zero_point) * scale;
}
}  // namespace

bool ml_init() {
    g_model = tflite::GetModel(g_model_data);
    if (g_model->version() != TFLITE_SCHEMA_VERSION) {
        MicroPrintf("Model schema mismatch: got %d expected %d", g_model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    static tflite::MicroMutableOpResolver<4> resolver;
    resolver.AddFullyConnected();
    resolver.AddRelu();
    resolver.AddSoftmax();
    resolver.AddQuantize();

    static tflite::MicroInterpreter static_interpreter(
        g_model, resolver, tensor_arena, kTensorArenaSize);
    g_interpreter = &static_interpreter;

    if (g_interpreter->AllocateTensors() != kTfLiteOk) {
        MicroPrintf("AllocateTensors failed");
        return false;
    }

    g_input = g_interpreter->input(0);
    g_output = g_interpreter->output(0);

    if (g_input == nullptr || g_output == nullptr) {
        MicroPrintf("input/output tensor missing");
        return false;
    }
    if (g_input->type != kTfLiteInt8 || g_output->type != kTfLiteInt8) {
        MicroPrintf("Expected int8 input/output tensors");
        return false;
    }

    return true;
}

bool ml_predict(float length_mm, float outer_diameter_mm, float weight_g, DetectionResult* out_result) {
    if (!g_input || !g_output || !out_result) return false;

    const float features[3] = {
        normalize_value(length_mm, kFeatureMin[0], kFeatureMax[0]),
        normalize_value(outer_diameter_mm, kFeatureMin[1], kFeatureMax[1]),
        normalize_value(weight_g, kFeatureMin[2], kFeatureMax[2]),
    };

    for (int i = 0; i < 3; ++i) {
        g_input->data.int8[i] = quantize_to_int8(features[i], g_input->params.scale, g_input->params.zero_point);
    }

    if (g_interpreter->Invoke() != kTfLiteOk) {
        MicroPrintf("Invoke failed");
        return false;
    }

    int best_idx = 0;
    float best_score = -1.0f;
    for (int i = 0; i < kNumClasses; ++i) {
        const float score = dequantize_from_int8(g_output->data.int8[i], g_output->params.scale, g_output->params.zero_point);
        out_result->scores[i] = score;
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    out_result->class_index = best_idx;
    return true;
}

const char* ml_label_from_index(int idx) {
    if (idx < 0 || idx >= kNumClasses) return "unknown";
    return kLabels[idx];
}
