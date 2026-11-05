# AirClick — Replacement Firmware

Replacement / repurposed firmware for the **Mellow FLY-AirClick** wireless button
board (ESP32-C3 + STM32F072).

## Goal

Repurpose the device as a **wireless-button → Home Assistant / GPIO** bridge:

- ESP32-C3 hosts a local WebUI (pairing, button configuration, GPIO/logical mapping).
- Button events are forwarded over **MQTT** (Home Assistant) — see `output_manager.c`.
- The RF receiver is a **pluggable interface** (stub now; the stock 2.4 GHz
  bit-banged protocol still needs the schematic/logic-analyzer to recover the
  exact pin + bitstream).

## Repository layout

```
docs/
  STAGE-1-2-REVERSE-ENGINEERING.md   # full reverse-engineering report
  FLASHING.md                        # mandatory flashing procedure
esp32/                               # ESP-IDF v5.4.1 project (ESP32-C3)
  main/
    app_main.c         # entry point (wires everything)
    config_manager.c   # NVS, versioned schema, safe defaults
    wifi_manager.c     # AP (config) + STA
    web_server.c       # HTTP REST + embedded WebUI (index.html)
    button_manager.c   # pairing + registry + simulated events
    event_manager.c    # thread-safe event queue
    output_manager.c   # MQTT (Home Assistant)
    rf_receiver.c      # RF receiver (stub; pluggable)
    index.html         # WebUI (single-page, dashboard/buttons/settings)
  build.sh             # reproducible build environment
```

## Build

```bash
cd esp32
./build.sh build        # -> build/airclick.bin
```

`build.sh` pins the environment (IDF_PATH, Python 3.12 venv, riscv32 toolchain,
CMake 3.29) to work around the broken system ESP-IDF export on this machine.

## Status

| Stage | Status |
|---|---|
| 1 — Reverse-engineer protocols | WebSocket API + config schema + button flow done. **RF bitstream pending** (needs schematic/LA). |
| 2 — Document pin mapping | ✅ `docs/STAGE-1-2-REVERSE-ENGINEERING.md` |
| 3 — WebUI (simulated events) | ✅ built (ESP-IDF, embedded SPA) |
| 4 — Persistent pairing/GPIO config | ✅ NVS, versioned schema |
| 5 — Output transport | ✅ MQTT (Home Assistant) |
| 6 — Integrate real wireless buttons | ⚠️ blocked on RF bitstream (stub interface in place) |
| 7 — STM32 firmware | reuse `airclick_serial_home_assistant` (USB-CDC) |
| 8 — End-to-end test | pending |

## Next step to finish the RF protocol

Without a schematic or logic analyzer, the last unknown is the **exact GPIO pin
and bit-stream timing** of the 2.4 GHz receiver. To complete it, either:
1. obtain the Mellow schematic (identifies the receiver chip + pin), or
2. capture the receiver's data pin during a button press with a logic analyzer.

Then implement `rf_receiver_init()` / the ISR in `esp32/main/rf_receiver.c` and
the real buttons will pair + fire events exactly like the simulated ones.
