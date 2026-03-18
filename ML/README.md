# ESP32 + TensorFlow Lite Micro Minimalprojekt

Dieses Projekt trainiert ein kleines TensorFlow-Modell für 3 Klassen (`screw`, `nut`, `washer`), quantisiert es auf `int8`, wandelt es in ein C-Headerfile um und zeigt, wie du es mit ESP-IDF auf einem ESP32 ausführst.

## 1) Python-Umgebung

```bash
python -m venv .venv
source .venv/bin/activate
pip install tensorflow numpy
```

## 2) Modell trainieren und nach TFLite exportieren

```bash
python train_and_export.py
python export_header.py
```

Danach hast du:

- `model_int8.tflite`
- `model_metadata.json`
- `main/model_data.h`

## 3) ESP-IDF-Projekt bauen

In einer ESP-IDF-Shell:

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Was das Modell erkennt

Das Beispiel klassifiziert einfache `Dinge` anhand von 3 Messwerten:

- `length_mm`
- `outer_diameter_mm`
- `weight_g`

Damit kannst du z. B. von Sensoren oder manuell gemessenen Werten zwischen

- `screw`
- `nut`
- `washer`

unterscheiden.

## An echte Sensoren anpassen

Ersetze in `main/main.cpp` die festen Beispielwerte durch echte Sensorwerte, zum Beispiel:

- Laserdistanz / ToF oder Schieblehre für Länge
- Hall/optische Messung oder Schieblehre für Durchmesser
- Wägezelle (HX711) für Gewicht

Dann rufst du einfach weiter `ml_predict(length_mm, outer_diameter_mm, weight_g, &result)` auf.

## Wichtig

- Das Modell ist absichtlich klein, damit es gut auf einen ESP32 passt.
- Für Kamera-/Bild-Erkennung brauchst du ein anderes, deutlich größeres Setup.
- Für einen ersten funktionierenden Workflow ist strukturierte Sensor-Klassifikation der einfachste Weg.
