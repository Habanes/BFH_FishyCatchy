import json
from pathlib import Path
import numpy as np
import tensorflow as tf

OUT = Path(__file__).resolve().parent
np.random.seed(42)
tf.random.set_seed(42)

# 3 features from simple sensors / measurements:
# [length_mm, outer_diameter_mm, weight_g]
FEATURE_MIN = np.array([1.0, 3.0, 0.1], dtype=np.float32)
FEATURE_MAX = np.array([80.0, 30.0, 40.0], dtype=np.float32)
LABELS = ["screw", "nut", "washer"]


def norm(x: np.ndarray) -> np.ndarray:
    return (x - FEATURE_MIN) / (FEATURE_MAX - FEATURE_MIN)


def make_class_samples(n: int, cls: str) -> np.ndarray:
    if cls == "screw":
        # long, small diameter, medium weight
        length = np.random.normal(42, 10, n).clip(15, 75)
        diameter = np.random.normal(5.5, 1.2, n).clip(3, 10)
        weight = (length * diameter * 0.06 + np.random.normal(0, 1.0, n)).clip(1, 18)
    elif cls == "nut":
        # short, larger diameter, compact and heavier than washer
        length = np.random.normal(8, 2.2, n).clip(3, 15)
        diameter = np.random.normal(12, 3.0, n).clip(6, 24)
        weight = (diameter * 0.22 + np.random.normal(0, 0.6, n)).clip(0.8, 8)
    elif cls == "washer":
        # very short, medium/large diameter, light
        length = np.random.normal(2.2, 0.6, n).clip(0.8, 4.5)
        diameter = np.random.normal(16, 4.0, n).clip(8, 28)
        weight = (diameter * 0.08 + np.random.normal(0, 0.3, n)).clip(0.2, 4)
    else:
        raise ValueError(cls)
    return np.stack([length, diameter, weight], axis=1).astype(np.float32)


def make_dataset(n_per_class: int = 700):
    xs, ys = [], []
    for idx, cls in enumerate(LABELS):
        xs.append(make_class_samples(n_per_class, cls))
        ys.append(np.full((n_per_class,), idx, dtype=np.int32))
    x = np.concatenate(xs, axis=0)
    y = np.concatenate(ys, axis=0)
    p = np.random.permutation(len(x))
    x = x[p]
    y = y[p]
    return norm(x).astype(np.float32), y


def representative_dataset(x_train: np.ndarray):
    for i in range(min(len(x_train), 200)):
        yield [x_train[i : i + 1].astype(np.float32)]


def main():
    x, y = make_dataset()
    split = int(len(x) * 0.8)
    x_train, x_test = x[:split], x[split:]
    y_train, y_test = y[:split], y[split:]

    model = tf.keras.Sequential([
        tf.keras.layers.Input(shape=(3,), name="measurements"),
        tf.keras.layers.Dense(16, activation="relu"),
        tf.keras.layers.Dense(12, activation="relu"),
        tf.keras.layers.Dense(len(LABELS), activation="softmax"),
    ])

    model.compile(
        optimizer=tf.keras.optimizers.Adam(1e-3),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"],
    )

    model.fit(x_train, y_train, validation_data=(x_test, y_test), epochs=35, batch_size=32, verbose=2)
    loss, acc = model.evaluate(x_test, y_test, verbose=0)
    print(f"test_accuracy={acc:.4f}, test_loss={loss:.4f}")

    saved_model_dir = OUT / "saved_model"
    model.export(saved_model_dir)

    converter = tf.lite.TFLiteConverter.from_saved_model(str(saved_model_dir))
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = lambda: representative_dataset(x_train)
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    tflite_model = converter.convert()

    tflite_path = OUT / "model_int8.tflite"
    tflite_path.write_bytes(tflite_model)
    print(f"wrote {tflite_path}")

    metadata = {
        "labels": LABELS,
        "feature_names": ["length_mm", "outer_diameter_mm", "weight_g"],
        "feature_min": FEATURE_MIN.tolist(),
        "feature_max": FEATURE_MAX.tolist(),
        "notes": "Inputs are normalized to [0,1] before inference on the ESP32.",
        "test_accuracy": float(acc),
    }
    (OUT / "model_metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(f"wrote {OUT / 'model_metadata.json'}")
    print("next: python export_header.py")


if __name__ == "__main__":
    main()
