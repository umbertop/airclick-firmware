# AirClick — Replacement Firmware

Replacement / repurposed firmware for the **Mellow FLY-AirClick** wireless-button board
(**ESP32-C3** + **STM32F072**), turning it into a **wireless-button → Home Assistant / GPIO**
bridge.

The stock product has two independent MCUs and no electrical link between them:

- **ESP32-C3** → Wi-Fi → printer (wireless 2.4 GHz buttons).
- **STM32F072** → USB → printer (10 wired buttons on PB0–PB9).

This repo replaces both firmwares and (optionally, with two jumper wires) bridges them so
wireless buttons appear on the same USB-CDC stream as the wired ones.

## What it does

- **ESP32-C3** hosts a local **WebUI** (button pairing, GPIO mapping, output-mode selection)
  and receives the 2.4 GHz wireless buttons on **GPIO10**.
- Button presses are forwarded either:
  - over **MQTT** (Home Assistant), or
  - over **serial → STM32 → USB-CDC** (appears as `PBx` on the host, like a wired button).
- **STM32F072** relays the 10 wired buttons over USB-CDC and, via a USART1 RX bridge,
  relays the ESP32's wireless-button data to the same USB-CDC stream.

## Repository layout

```
docs/
  STAGE-1-2-REVERSE-ENGINEERING.md   # full reverse-engineering report
  FLASHING.md                        # flashing procedure + SHA-256 checksums
esp32/                               # ESP-IDF v5.4.1 project (ESP32-C3)
  main/
    app_main.c         # entry point
    config_manager.c   # NVS, versioned schema, safe defaults
    wifi_manager.c     # AP (always on) + STA (optional)
    web_server.c       # HTTP REST + embedded WebUI (index.html)
    button_manager.c   # pairing + registry
    event_manager.c    # thread-safe event queue
    output_manager.c   # MQTT or STM32 serial
    rf_receiver.c      # GPIO10 ISR + level-aware bit decode
    index.html         # WebUI (dashboard / buttons / settings)
  build.sh             # reproducible build environment
stm32/                               # STM32Cube HAL project (STM32F072)
  Core/                # main.c, EXTI buttons, USART1 RX bridge
  USB_DEVICE/          # USB-CDC interface
  Drivers/ Middlewares/# HAL + CMSIS + USB device library
  Makefile             # -> build/umb_airclick.bin
esp32-probe/                         # GPIO/RF capture helper (RE tooling)
reference/                           # original firmware segments
```

## Build

### ESP32 (ESP-IDF v5.4.1)

```bash
cd esp32
./build.sh build          # -> build/airclick.bin
```

`build.sh` pins the environment (IDF_PATH, Python 3.12 venv, riscv32 toolchain, CMake 3.29)
to work around a broken system ESP-IDF export.

### STM32 (arm-none-eabi-gcc + STM32Cube HAL)

```bash
cd stm32
make                      # -> build/umb_airclick.bin
```

## Flash

> Both chips have **separate USB ports** (both Type-C). See `docs/FLASHING.md` for the
> full procedure, checksums and recovery.

### ESP32-C3 (USB-Serial-JTAG)

```bash
cd esp32/build
esptool.py --chip esp32c3 -b 460800 --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0x10000 airclick.bin
```

Settings (Wi-Fi, paired buttons, output mode) live in NVS and are **preserved** across
re-flashes — only `erase-flash` wipes them.

### STM32F072 (USB DFU)

Enter DFU mode (hold **BOOT** while connecting the STM32's Type-C), then:

```bash
cd stm32
dfu-util -a 0 -d 0483:df11 --dfuse-address 0x08000000 -D build/umb_airclick.bin
```

## Usage

1. **Power the ESP32** and join its access point **`AirClick`** (open, no password).
2. Open **http://192.168.4.1** (the device's hostname is `AirClick`).
3. **Pair** a wireless button (Buttons → Start pairing → press the button, or add by ID).
4. **Assign** each button a logical GPIO (0–9).
5. Choose the **output mode** in Settings:

| Mode | Behaviour |
|---|---|
| **Home Assistant (MQTT)** | publishes `<topic>/<id>/state` and `<topic>/<id>/event` to the broker |
| **STM32 (serial)** | sends `PBx` (the button's GPIO) over UART1 at 115200 baud |

For the MQTT path the ESP32 also needs to **join your Wi-Fi** (Settings → Mode → "Join
existing Wi-Fi") and a broker configured in the MQTT section.

## Wiring (STM32 path only)

The board has no UART between the MCUs, so the serial path needs two jumpers:

| From | To |
|---|---|
| ESP32 **GPIO4 (TX)** | STM32 **PA10 (RX)** |
| GND | GND |

`PA9`/`GPIO5` for the reverse direction is not needed for button data.

## Status

| Area | Status |
|---|---|
| Reverse-engineering | ✅ done — `docs/STAGE-1-2-REVERSE-ENGINEERING.md` |
| WebUI (pairing, GPIO mapping, settings) | ✅ |
| RF receiver (GPIO10) | ✅ level-aware decode + majority-vote signature |
| Output — MQTT | ✅ |
| Output — STM32 serial | ✅ (ESP32 `PBx` → STM32 USART1 → USB-CDC) |
| Wired buttons (STM32) | ✅ `PBx\r\n` over USB-CDC |
| Persistent config (NVS) | ✅ versioned schema |

The RF button ID is a stable signature hash (`FLYxxxxxxxx`) rather than the original
32-bit ID; pairing/matching work, but IDs won't match a stock device.
