# AirClick — Stage 1 & 2 Reverse-Engineering Report

> Scope: reverse-engineer the existing firmware, document the hardware architecture,
> GPIO/pin mapping, partition layout, configuration schema, and flashing procedures.
> This document is the source of truth for all subsequent development stages.

---

## 1. Executive summary

The device is a **Mellow "FLY-AirClick"** wireless button board for 3D printers
(Klipper / RRF / Bambu Lab). It is **not** the architecture assumed in the original
requirements: there is **no I²C link** between the two MCUs, and the STM32 GPIOs are
**inputs** (button sensing), not outputs.

| | |
|---|---|
| **Product** | Mellow FLY-AirClick (model "WBTN", group "FLY") |
| **Vendor** | 3D Mellow — `https://mellow.klipper.cn`, shop `https://3dmellow.com` |
| **Main MCU** | Espressif **ESP32-C3** (Wi-Fi + BLE + 2.4 GHz RF) |
| **Aux MCU** | STMicroelectronics **STM32F072C8** (64 KB flash / 16 KB RAM) |
| **Power** | 5 V via Type-C **or** PH2.0-2P terminal |
| **Antenna** | ipex3 (receives wireless-button signals + Wi-Fi) |

### Intended repurpose (per project direction)
Wireless buttons → ESP32-C3 (WebUI + pairing) → output via **serial / MQTT / GPIO**
(reusing the existing `airclick_serial_home_assistant` STM32 USB-CDC work), instead of
the stock "send G-code to a 3D printer" behaviour.

---

## 2. Hardware architecture

```
                     ┌─────────────────────────────┐
  Wireless buttons   │        ESP32-C3 (main)      │
  (CR927, 2.4GHz RF) │  Wi-Fi  -> Klipper/RRF/Bambu│  (stock behaviour)
  ═══════════════►   │  BLE/RF <- button signals    │
                     │  WebUI :80 (WebSocket /ws)   │
                     │  LittleFS www + config       │
                     └─────────────────────────────┘

                     ┌─────────────────────────────┐
  Wired buttons      │        STM32F072 (aux)      │
  PB0..PB9 (10x)     │  USB-CDC (PA11/PA12)         │
  ═══════════════►   │  runs Klipper firmware       │
                     └─────────────────────────────┘
```

- **Two independent paths.** The stock product has the ESP32 and STM32 doing separate
  jobs with **no direct electrical link between them** (no I²C, no UART between the two
  chips was found in the docs or firmware strings).
  - ESP32-C3 → Wi-Fi → printer (wireless buttons, unlimited commands).
  - STM32F072 → USB → printer (10 wired buttons, works offline).
- **Two USB ports**: the ESP32's USB and the STM32's "flashing port" (both Type-C).

---

## 3. Artifact inventory

| Path | Size | SHA-256 |
|---|---|---|
| `~/Downloads/airclick/airclick-full.bin` | 4 194 304 (4 MB) | `5d4537de8c98949f75537b2f8226f33359dcac30bf43fea6d513b62cc252cc3e` |
| `~/Downloads/airclick/airclick-lfs_www.bin` | 1 507 328 (1.5 MB) | `6002342563e6993c7156a3b12fa4d647e726df8f4fd053c78f9a200ff92c87ff` |
| `~/Downloads/airclick/combine/target.bin` | 4 194 304 | identical to `airclick-full.bin` (combined flash) |
| `~/firmware.bin` | 37 772 | `bfe68eb752f516c1ff46bcc051d002f116589da7b4b58e62c5ec9178368837a5` |
| `~/airclick_serial_home_assistant/build/umb_airclick.bin` | — | `7201bd8c5910546a979769f7e250f87256e107e3ee1fa338134ea7e135069b77` |

- `airclick-full.bin` is a **complete 4 MB ESP32-C3 flash image** (bootloader + partition
  table + app + `config` LittleFS + `www` LittleFS). It is the master copy — all
  sub-partitions can be re-derived from it.
- `airclick-lfs_www.bin` is the `www` partition only (LittleFS v2).
- `~/firmware.bin` is **unidentified**: it is a Cortex-M image (initial SP `0x20024000`,
  reset `0x08007D6D`, Thumb) containing `CanBoot!` and reversed `USB BOOT` strings. Its
  144 KB RAM footprint does **not** match STM32F072 (16 KB). Likely a CanBoot/Klipper
  bootloader image for another target — **investigate before assuming it is AirClick
  related.** (See §14.)

---

## 4. ESP32-C3 flash / partition layout

Bootloader @ `0x0`, partition table @ `0x8000` (binary CSV, magic `0x50AA`):

| # | Label | Type/Subtype | Offset | Size | Notes |
|---|---|---|---|---|---|
| 1 | `nvs` | 0x01 / 0x02 | `0x9000` | 20 KB | Non-volatile storage |
| 2 | `otadata` | 0x01 / 0x00 | `0xE000` | 8 KB | OTA data |
| 3 | `app0` | 0x00 / 0x10 | `0x10000` | 2 MB | Factory app |
| 4 | `config` | 0x01 / 0x82 | `0x210000` | 512 KB | LittleFS — `configs.json` |
| 5 | `www` | 0x01 / 0x82 | `0x290000` | 1 472 KB | LittleFS — web UI |

- `www` image is LittleFS v2, block size 4096, 368 blocks.
- `config` image is LittleFS v2, block size 4096, 128 blocks.
- No OTA (`app1`) partition — single factory app slot.

---

## 5. Configuration schema (`config/configs.json`)

```json
{
  "net":     { "ssid": "", "pass": "", "bssid": "", "channel": 0,
               "ssid_ap": "", "pass_ap": "", "is_ap": false, "hostname": "" },
  "printer": { "platform": 0, "ip": "", "port": 80, "apikey": "", "uuid": "" },
  "info":    { "brand": "Mellow", "group": "FLY", "model": "WBTN", "url": "3dmellow.com" },
  "system":  { "lang": "zh-CN", "ntp": "pool.ntp.org", "ntp2": "ntp.ntsc.ac.cn", "beep": true },
  "buttons": {}
}
```

- `net` — Wi-Fi station/AP settings (SSID/pass, AP mode toggle).
- `printer` — Moonraker connection (`platform` selects Klipper/RRF/BambuLab, `ip`/`port`/`apikey`).
- `buttons` — map of paired wireless buttons (empty in the captured dump).
- Persistent storage lives in the **`config` LittleFS partition**, not NVS.

---

## 6. Web UI / HTTP API surface

Web UI = Vite/Vue single-page app, `index.html` + hashed assets, stored gzip-compressed
in the `www` LittleFS partition under `static/`.

Files extracted from `www`:

| File | Size | Purpose |
|---|---|---|
| `index.html` | 709 B | SPA shell, `<title>FLY-AirClick</title>` |
| `static/759d7f79.js` | 274 KB (gzip→) | Main app bundle (obfuscated) |
| `static/435df2db.css` | 92 KB | Main stylesheet |
| `static/730ac325.js`, `dbde4123.js` | 8.6 / 15.6 KB | Vendor/lazy chunks |
| `static/229f7add.png` | 197 KB | Image asset |
| `static/e52d60f6.woff2` | 377 KB | Font |

**HTTP endpoints** (recovered from firmware strings):

| Endpoint | Purpose |
|---|---|
| `/` → `index.html`, `/static/*`, `/favicon.ico` | Static web UI |
| `/ws` | **WebSocket** (real-time button events / status) |
| `/config`, `/configs.json` | Config read |
| `/api/config/download` | Config export |
| `/api/config/upload` | Config import |
| `/printer/gcode/script?script=…` | Send G-code |
| `/printer/print/start?filename=…` | Start print |
| `/printer/print/pause` / `resume` / `cancel` | Print control |
| `/qq_product_operations/nettest/`, `/connecttest.txt`, `/msftconnecttest.com/redirect` | Captive-portal / connectivity probe |
| `/dev/uart` | UART passthrough (needs investigation) |

**JSON message vocabulary** (from firmware strings):

- `{"state":0, "msg":"Button Trigger", "id":"<button id>"}`
- `{"command":"gcode_line", "gcode":"…"}`
- `{"state":0, "msg":"printer connected"}` / `{"state":1, "msg":"printer disconnected"}`
- `{"state":2, "msg":"Invalid button id"}` / `{"state":4, "msg":"Duplicate button id"}` / `{"state":5, "msg":"add button failed"}`
- `{"state":2, "msg":"klipper error", "code":…}` / `{"state":1, "msg":"Failed to run gcode", "id":"…"}`
- Actions: `button_add`, `button_delete`, `button_edit`, `button_listen`, `button_error`

---

## 7. Wireless button pairing (what is known)

- Buttons use a **CR927** coin cell.
- Pairing flow (from vendor docs): WebUI home → click `+` → press the wireless button →
  its **unique ID auto-generates**. Name it, then assign an action.
- Button actions are one of:
  1. **Custom GCODE** (send a macro/G-code line to the printer via Wi-Fi), or
  2. **Request API** (arbitrary Moonraker/RRF/Bambu HTTP API call: method + params), or
  3. **Trigger GPIO** (Klipper-only; maps to STM32 `PB0..PB9` via the wired/USB path).
- The wireless link itself is **proprietary 2.4 GHz RF** received by the ESP32-C3 antenna.
  **Not yet reverse-engineered** — this is the main Stage-1 open item (§14), to be done in
  Ghidra against `app0` (RISC-V).

---

## 8. STM32 wired buttons & pin mapping

From `~/airclick_serial_home_assistant/umb_airclick.ioc` (STM32F072C8Tx):

| Pin | Function | Config |
|---|---|---|
| **PB0 – PB9** | Wired button inputs | `GPIO_MODE_IT_RISING`, pull-down, EXTI |
| **PA11** | USB DM | Device |
| **PA12** | USB DP | Device |

- Clock: **HSI48** (48 MHz, USB clock source), no PLL.
- Interface: **USB-CDC** (virtual COM port).
- Stock firmware for wired mode is **Klipper** (`[mcu AirClick] serial: …usb-Klipper_stm32f072xb_AirClick-if00`),
  exposing `PB0..PB9` as `^!AirClick:PBx` `gcode_button` pins.
- Your existing reimplementation (`airclick_serial_home_assistant`, project `umb_airclick`)
  instead reads PB0–PB9 and prints `PBx\r\n` over USB-CDC (verified working via
  `~/tmp/airclick_serial.log`).

---

## 9. Flashing procedures

### 9.1 ESP32-C3 (main firmware)

- **Tool**: Espressif `flash_download_tool_3.9.4` (Windows), `ChipType=ESP32-C3`,
  `WorkMode=Develop`, `LoadMode=USB` (native USB, no UART adapter needed).
- **Entry**: with AirClick **powered off**, **hold BOOT** while connecting the ESP32 USB
  Type-C cable to the computer.
- **Flash addresses**:
  | Image | Address | Purpose |
  |---|---|---|
  | `*-full.bin` | `0x0` | full flash (wipes config) |
  | `*-delta.bin` | `0x10000` | app only (keeps config) |
  | `*-www.bin` | `0x290000` | web UI only |
- Max baud `921600`; wait for completion before unplugging.
- On Linux the ESP32-C3 native USB enumerates as a USB-serial (e.g. `/dev/ttyACM0`) via
  USB-Serial-JTAG when flashed with the CDC console enabled; the ROM bootloader appears as
  a **USB Serial/JTAG** device (`esptool.py --chip esp32c3` autodetects it).

### 9.2 STM32F072 (wired-mode MCU)

- **Interface**: USB **DFU** bootloader (STM32 system memory), **not** reachable through the
  ESP32 USB connection.
- **Entry**: with AirClick **powered off**, **hold the STM32 BOOT button** while connecting
  the STM32 flashing port (Type-C) to the computer.
- **Confirm DFU**: `lsusb` should show `0483:df11`.
- **Flash** (Klipper or custom firmware):
  ```bash
  sudo dfu-util -a 0 -d 0483:df11 --dfuse-address 0x08000000 -D <firmware.bin>
  ```
- **Full erase** (destroys bootloader — only for recovery):
  ```bash
  sudo dfu-util -a 0 -d 0483:df11 -s 0x08000000:mass-erase:force
  ```
- After flashing, **power-cycle** once. USB ID becomes
  `usb-Klipper_stm32f072xb_AirClick-if00` (for Klipper firmware).
- Your project's README uses the same `dfu-util` invocation for `build/umb_airclick.bin`.

---

## 10. Toolchain & environment

- **ESP-IDF v5.4** installed at `~/esp/esp-idf`; RISC-V toolchain at
  `~/.espressif/tools/riscv32-esp-elf` (plus `xtensa-esp-elf`, `openocd-esp32`).
- **PlatformIO** installed (`~/.platformio`) with `framework-arduinoespressif32`,
  `framework-espidf`, `tool-mklittlefs`, `tool-esptoolpy`.
  - Note: the **stock firmware was built with PlatformIO + Arduino-ESP32** (project `wbtn`),
    not bare ESP-IDF. A clean-room replacement may use bare ESP-IDF v5.4 per requirements.
- **`littlefs-python`** (in `/tmp/lfsvenv`) successfully unpacks the LittleFS images.
  - ⚠️ **Safety note**: `littlefs-python` mounts **read-write** and mutates the image on
    unmount. Always extract from a **copy**, never the master `airclick-full.bin` / dump.
    (This already happened once; `airclick-lfs_www.bin` was restored from
    `airclick-full.bin` — see §14.)

---

## 11. Key discrepancies vs. the requirements document

| Requirement assumption | Reality |
|---|---|
| ESP32 ↔ STM32 over "existing I²C" (§5) | **No I²C link exists.** The two MCUs are independent (ESP32=Wi-Fi, STM32=USB). |
| STM32 GPIOs are **outputs** (§4) | STM32 `PB0–PB9` are **button inputs** (EXTI rising, pull-down). |
| "ESP32 forwards events to STM32 → output/serial" | Stock flow is wireless→ESP32→**Wi-Fi→printer**; wired→STM32→**USB→printer**. |
| Buttons → logical GPIO output | Stock actions are **G-code / API / Klipper GPIO trigger**, not physical GPIO out. |

These mismatches are why the project was re-scoped to **"Repurpose for Home Assistant /
GPIO"** rather than a drop-in AirClick clone.

---

## 12. Architecture decisions carried forward (for the replacement firmware)

1. **Repurpose** the device as a wireless-button → Home-Assistant / GPIO bridge.
2. ESP32-C3 hosts the WebUI, pairing, and config (NVS or the `config` LittleFS partition).
3. Output path(s) to be confirmed (see §14): serial (UART/USB-CDC), MQTT, and/or the
   existing `airclick_serial_home_assistant` STM32 USB-CDC firmware.
4. Wireless-button protocol must be recovered before real buttons integrate (Stage 6);
   the WebUI can be built with simulated events meanwhile (Stage 3).

---

## 13. Development-stage mapping (updated)

| Stage | Status |
|---|---|
| 1 — Reverse-engineer wireless + I²C protocols | ⚠️ Wireless RF protocol **pending** (Ghidra). No I²C to RE (doesn't exist). |
| 2 — Document GPIO/pin mapping | ✅ This document (§8). |
| 3 — WebUI with simulated events | Next after this doc. |
| 4 — Persistent pairing + GPIO config | Pending. |
| 5 — (was "original I²C protocol") | Re-scoped → output transport (serial/MQTT/GPIO). |
| 6 — Integrate real wireless buttons | Blocked on Stage 1 RF reverse-engineering. |
| 7 — STM32 replacement firmware | Partially exists (`umb_airclick` USB-CDC). |
| 8 — End-to-end test | Pending. |

---

## 14. Stage-1 deep-dive results (Ghidra)

### 14.1 WebSocket command API (complete, from dispatcher `FUN_ram_4200394a`)

The WebSocket `/ws` handler is a string-dispatch over the JSON `command` field. Commands:

| Command | Purpose |
|---|---|
| `update_config` | Write config (`configs.json`) |
| `get_version` | Firmware/version info |
| `update_printer` | Set printer (Moonraker/RRF/Bambu) connection |
| `net_status` | Report network status |
| `reboot` | Reboot ESP32 |
| `set_wifi` | Set Wi-Fi station/AP config |
| `scan_wifi` | Scan Wi-Fi APs |
| `connect_wifi` | Connect to Wi-Fi |
| `connect_printer` | Connect to printer (Moonraker) |
| `button_listen` | Start/stop listening for wireless buttons |
| `button_add` | Pair a button |
| `button_delete` | Remove a button |
| `button_edit` | Edit a button |
| `set_beep` | Enable/disable buzzer |
| `scan_bambulab` | Discover BambuLab printers on LAN |
| `test_test1` | **Simulated button trigger** (ID `FLY12345678`) |

### 14.2 Wireless-button reception mechanism

- **Confirmed RF pin: GPIO10** (found via a GPIO-probe firmware flashed to the device —
  it is the only pin that toggles).
- **Not BLE** (no `esp_bt_controller_init`/`esp_ble_gap`/GATT), **not ESP-NOW**, **not Wi-Fi
  promiscuous/sniffer**, **not I²C/SPI/UART** (the `/dev/uart` hit is just the ESP-IDF VFS
  console; `sda` was `Wedne**sda**y`).
- The ESP32 **does** use **GPIO interrupts**:
  `gpio_install_isr_service` (×2), `gpio_isr_handler_add`, Arduino `attachInterrupt`,
  `gpio_set_level`.
- **Live capture** (logic-analyzer-style, µs resolution) on GPIO10 shows:
  - ambient: periodic ~14.9 ms HIGH "squelch" pulses;
  - button press: ~11 ms LOW sync gap, then a burst of ~380 µs / ~1100 µs pulses
    (≈160–280 edges) encoding the 32-bit button ID.
- **Conclusion**: the wireless buttons are received by a **separate 2.4 GHz RF receiver
  chip** whose demodulated output is a **bit-banged digital signal on GPIO10**, decoded by
  an ESP32 interrupt handler.

### 14.3 Button identifier format

`FLY` + 8 hex chars, e.g. `FLY12345678` (used by the `test_test1` simulated-trigger command
and echoed in `{"state":0,"msg":"Button Trigger","id":"<id>"}`).

### 14.5 Button event flow (traced end-to-end)

```
RF receiver ISR (GPIO interrupt, bit-banged)
   -> writes button event into state struct @0x3fc91b20
   -> button poll task FUN_ram_42015c02  (every 10ms)
      -> button-action executor FUN_ram_4200533c
         action 1 = G-code (Moonraker /printer/gcode/script)
         action 2 = Moonraker API (method+params)
         action 3 = GPIO trigger (toggle STM32 output)
         + beep patterns (LEDC, delay 0x3c/0x28/100/200 ms)
```

- Entry/reset: `0x40382360` (IRAM). `app_main` = `0x4203851a` (Arduino-core main).
- WebSocket dispatcher: `FUN_ram_4200394a`. Button poll task: `FUN_ram_42015c02`.
- Button executor: `FUN_ram_4200533c`. `gpio_isr_handler_add`/`attachInterrupt` present
  but the exact RF pin + bitstream is **not statically extractable** (stripped binary,
  RISC-V string xrefs not auto-resolved by Ghidra). Needs schematic or logic analyzer.

### 14.4 Peripheral inventory (confirmed)

Wi-Fi (STA+AP), UART0 console (VFS), LEDC PWM (buzzer "beep" + status LED), GPIO +
GPIO-interrupt (RF receiver decode). No BLE, no ESP-NOW, no I²C, no SPI peripheral.

---

## 15. Open questions / next steps (updated)
1. **Wireless RF bitstream** (blocking Stage 6): load **seg4 (IRAM)** into Ghidra to
   disassemble the GPIO ISR — recover the exact **GPIO pin**, **frame timing/encoding**
   (Manchester? pulse widths?), and **button-ID bit layout**. Also load **seg0** so
   `list_strings` + `get_xrefs_to` work natively.
2. **`~/firmware.bin`** identity (37 KB Cortex-M, `CanBoot!`, 144 KB RAM) — still
   unresolved; confirm whether AirClick-related.
3. **Output transport** decision (serial/MQTT/GPIO) for the Home-Assistant repurpose.
4. **Schematic / pin-level truth** (vendor cloud link) to confirm the RF receiver chip and
   its data pin.
5. **BOOT buttons / two Type-C ports** — confirm physical layout on the hardware in hand.
