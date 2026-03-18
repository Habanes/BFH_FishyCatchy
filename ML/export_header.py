from pathlib import Path

IN_FILE = Path(__file__).resolve().parent / "model_int8.tflite"
OUT_FILE = Path(__file__).resolve().parent / "main" / "model_data.h"
ARRAY_NAME = "g_model_data"


def main():
    data = IN_FILE.read_bytes()
    lines = []
    lines.append("#pragma once")
    lines.append("#include <cstddef>")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append(f"alignas(16) const unsigned char {ARRAY_NAME}[] = {{")

    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        hexes = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hexes},")

    lines.append("};")
    lines.append(f"constexpr std::size_t g_model_data_len = {len(data)};")
    OUT_FILE.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {OUT_FILE}")


if __name__ == "__main__":
    main()
