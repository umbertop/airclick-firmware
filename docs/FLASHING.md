# Flashing the AirClick replacement firmware

> Applies to the **ESP32-C3** (main MCU). The STM32F072 is a separate MCU with
> its own USB/DFU procedure and is **not** flashed here.

## 1. Firmware target

| | |
|---|---|
| Chip | Espressif **ESP32-C3** (RISC-V, 4 MB flash) |
| Framework | ESP-IDF **v5.4.1** |
| Partition table | single-app (factory `app` @ `0x10000`, 1 MB) |
| Build result | `Project build complete` (exit 0) |

## 2. Firmware files and checksums

| File | SHA-256 |
|---|---|
| `build/bootloader/bootloader.bin` | `fa08544501d3701d00f4a4e4b13bac675e4e297fa8541a97f316df389290b971` |
| `build/partition_table/partition-table.bin` | `7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820` |
| `build/airclick.bin` | `9b654772eef2ec44b096fffd0918c2b6fba084f150c32ba536f106a2a85e5f3a` |

Full paths under `/home/umberto/airclick-firmware/esp32/build/`.

## 3. Bootloader procedure (ESP32-C3)

The ESP32-C3 has **native USB** (USB-Serial-JTAG). To enter the ROM bootloader:

1. **Disconnect** the AirClick's ESP32 USB cable.
2. **Press and hold the BOOT button** (GPIO9).
3. **Reconnect** the USB cable **while keeping BOOT pressed**.
4. Wait ~1 s, then **release BOOT** (safe once the device has enumerated).

### Identify the serial port on Arch Linux

The ESP32-C3's native USB appears as a USB CDC/ACM device:

```bash
ls /dev/ttyACM*
# and/or
dmesg | tail -20   # look for "cdc_acm" / "ttyACM0" / "USB JTAG"
```

It is typically `/dev/ttyACM0`. (The STM32's USB-CDC also shows up as a
`ttyACM*`; if two appear, unplug the ESP32 cable and see which disappears.)

## 4. Flash (with `esptool.py`)

From the build directory:

```bash
cd /home/umberto/airclick-firmware/esp32/build
python3.12 -m esptool --chip esp32c3 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x0 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0x10000 airclick.bin
```

Or use the IDF wrapper (equivalent):

```bash
cd /home/umberto/airclick-firmware/esp32
./build.sh -p /dev/ttyACM0 flash
```

> ⚠️ **Do not flash until you have confirmed the serial port.** `esptool` will
> also tell you the chip type it detected — it must report **ESP32-C3**.

> ✅ **Your settings are preserved.** This command writes only the bootloader
> (`0x0`), partition table (`0x8000`) and app (`0x10000`). It never touches the
> NVS partition (`0x9000`), which holds the Wi-Fi credentials, paired buttons and
> output mode — so re-flashing an update keeps all of your configuration.
> Only `erase-flash` (recovery, §6) wipes settings.

## 5. After flashing

- The device reboots automatically (`--after hard_reset`).
- **Verify boot** by opening the serial console and looking for the banner:

  ```bash
  # with minicom / picocom / screen
  picocom -b 115200 /dev/ttyACM0
  ```

  Expected log line: `AirClick replacement firmware v0.1.0 booting`, then
  `ready. AP SSID: AirClick`.

- **Connect** to the access point **`AirClick`** (open, no password), then open
  the web UI at **`http://192.168.4.1/`** (ESP32 AP default gateway).

## 6. Recovery if the firmware fails to start

- The factory bootloader is intact (we only wrote the app + partition table +
  bootloader). Re-enter the ROM bootloader (BOOT + plug) and re-flash.
- To wipe settings, run `./build.sh -p /dev/ttyACM0 erase-flash` (erases NVS +
  app; then re-flash).
- If the serial console shows repeated `rst:...` / boot loops, capture the log
  and check the backtrace against `build/airclick.elf`.
