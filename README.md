# moto

C firmware for the **ESP32-C5** microcontroller (RISC-V) with **OTA
self-update** over WiFi, built on
[ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/) v5.5.

## Features

- **Breathing LED** using hardware LEDC PWM (zero CPU overhead)
- **WiFi 6** connectivity (dual-band 2.4 + 5 GHz)
- **OTA firmware updates** from GitHub Releases with A/B partition rollback
- **Automatic rollback** — if new firmware fails to boot, the bootloader
  reverts to the previous version

## Project structure

```
main/
  main.c              Entry point: NVS, WiFi, OTA, LED
  wifi.c / wifi.h     WiFi station connection
  ota.c / ota.h       OTA update client (checks GitHub Releases)
  breathing.c / .h    Breathing LED cycle (LEDC PWM)
  Kconfig.projbuild   WiFi credentials config
  CMakeLists.txt      Component registration
CMakeLists.txt         Top-level ESP-IDF project file
sdkconfig.defaults     ESP-IDF configuration overrides
Dockerfile             Docker build environment
```

## Quick start (Docker — no local toolchain needed)

```bash
export WIFI_SSID="YourNetwork"
export WIFI_PASS="YourPassword"

docker compose build
```

The firmware binary is at `build/moto.bin` inside the container.

## Local development

### Prerequisites

1. **ESP-IDF v5.5** — follow the
   [install guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/get-started/index.html)
2. Source the environment: `. $HOME/esp/esp-idf/export.sh`

### Build & flash

```bash
idf.py set-target esp32c5
idf.py menuconfig   # set WiFi SSID/password under "Moto Configuration"
idf.py build flash monitor
```

### Configure WiFi without menuconfig

```bash
# Append to sdkconfig.defaults before building:
echo 'CONFIG_MOTO_WIFI_SSID="YourNetwork"' >> sdkconfig.defaults
echo 'CONFIG_MOTO_WIFI_PASS="YourPassword"' >> sdkconfig.defaults
idf.py build
```

## How it works

### OTA update flow

1. On boot, the firmware connects to WiFi and checks GitHub Releases for a
   newer `moto.bin`.
2. If found, it streams the binary into the inactive flash partition (A/B
   scheme).
3. After verification the device reboots into the new firmware.
4. The new firmware marks itself as valid; if it fails to do so, the
   bootloader automatically rolls back to the previous version.

### Flash partition layout

```
┌──────────────────────┐
│  Bootloader          │
├──────────────────────┤
│  Partition Table     │
├──────────────────────┤
│  NVS (24 KB)         │  WiFi creds, config
├──────────────────────┤
│  OTA Data (8 KB)     │  Tracks active slot
├──────────────────────┤
│  PHY Init (4 KB)     │  WiFi/BT calibration
├──────────────────────┤
│  ota_0 (1600 KB)     │  App slot A
├──────────────────────┤
│  ota_1 (1600 KB)     │  App slot B
└──────────────────────┘
```

### CI pipeline

The GitHub Actions workflow (`.github/workflows/ci.yml`):

1. **Build** — compiles firmware in the ESP-IDF Docker container
2. **Publish** — uploads `moto.bin` to GitHub Releases (the OTA update source)

## License

See [LICENSE](LICENSE).
esptool.py --chip esp32c5 --port /dev/cu.usbmodem\* -b 460800 write_flash 0x0 build/firmware.bin
