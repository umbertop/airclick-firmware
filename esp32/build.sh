#!/usr/bin/env bash
# Build helper for the AirClick ESP32 firmware.
# Reproduces the ESP-IDF environment (workaround for the broken export.sh /
# Python-3.14 upgrade on this machine).
set -e

export IDF_PATH=/home/umberto/esp/esp-idf
export IDF_TOOLS_PATH=/home/umberto/.espressif
export IDF_PYTHON_ENV_PATH=/home/umberto/.espressif/python_env/idf5.4_py3.12_env
export ESP_ROM_ELF_DIR=/home/umberto/.espressif/tools/esp-rom-elfs/20241011/

export PATH="/home/umberto/.espressif/python_env/idf5.4_py3.12_env/bin:\
/home/umberto/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20241119/riscv32-esp-elf/bin:$PATH"

cd "$(dirname "$0")"
exec python3.12 "$IDF_PATH/tools/idf.py" "$@"
