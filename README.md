# moto

[Matter](https://csa-iot.org/all-solutions/matter/)-enabled LED controller
firmware for the **ESP32-C5** (RISC-V), built on
[ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/) v5.5.1
and the [ESP-Matter](https://github.com/espressif/esp-matter) SDK.

## Features

- **6 independent Matter dimmable lights** — each LED is a separate endpoint
  with its own on/off and brightness control, plus a **Mode Select** cluster
  that exposes a selectable role dropdown (e.g. "left front indicator",
  "taillight", "main light")
- **Always-off power-on behavior** — all LEDs start off after every power
  cycle; the OnOff Lighting feature is omitted so no `StartUpOnOff` attribute
  is exposed and no "Power On Behavior" dropdown appears in Matter controllers
- **Hardware LEDC PWM** with gamma correction for perceived-linear brightness
- **WiFi 6** connectivity (dual-band 2.4 + 5 GHz)
- **BLE commissioning** — pair with any Matter controller (Apple Home, Google
  Home, etc.)
- **OTA firmware updates** via the Matter OTA Requestor with A/B partition
  rollback
- **Matter shell** for runtime diagnostics over UART

## Project structure

```
main/
  app_main.cpp         Entry point: Matter node, 6 dimmable-light endpoints
  app_driver.cpp/.h    Bridges Matter attributes to individual LEDs
  breathing.c/.h       Per-channel LEDC PWM driver with gamma correction
  idf_component.yml    ESP-Matter and button dependencies
  CMakeLists.txt       Component registration
CMakeLists.txt         Top-level ESP-IDF project file
sdkconfig.defaults     ESP-IDF / Matter configuration overrides
partitions.csv         Flash partition table (OTA A/B scheme)
Dockerfile             Docker build environment (ESP-IDF + ESP-Matter)
docker-compose.yml     Docker Compose build service
```

## Build

```bash
docker compose run --rm --build build
```

The merged flashable binary is output as `build/firmware.bin`.

## Flash

There are two ways to flash depending on your hardware setup.

### Option A: USB-to-UART adapter via USB/IP (supports Docker)

With an external USB-to-UART adapter (CP2102, CH340, FTDI) wired to the
ESP32-C5's UART0, flashing works from Docker at full speed via USB/IP. The
adapter doesn't re-enumerate during the download-mode reset (unlike the
built-in USB-Serial/JTAG), so the USB/IP attachment stays stable.

Wiring:

| Adapter | ESP32-C5     |
| ------- | ------------ |
| TX      | RX (GPIO17)  |
| RX      | TX (GPIO16)  |
| DTR     | EN (reset)   |
| RTS     | GPIO9 (boot) |
| GND     | GND          |

Install and start the USB/IP server on the host:

```bash
pip install libusb1
git clone https://github.com/tumayt/pyusbip /tmp/pyusbip
sudo python3 /tmp/pyusbip/pyusbip.py > /dev/null
```

Find the adapter's bus-id (in a second terminal):

```bash
docker run --rm -it --privileged --pid=host alpine \
  nsenter -t 1 -m usbip list -r host.docker.internal
```

Flash from Docker (set `USBIP_BUS_ID` to the adapter's bus-id from above):

```bash
USBIP_BUS_ID=1-1 docker compose run --rm flash
```

Factory-reset flash (erases NVS, Matter fabric, WiFi credentials, OTA state):

```bash
USBIP_BUS_ID=1-1 docker compose run --rm flash-factory
```

Stop the USB/IP device manager when done:

```bash
docker compose down devmgr
```

### Option B: Built-in USB-Serial/JTAG (host only)

The built-in USB-Serial/JTAG (`/dev/cu.usbmodem*`) requires direct USB access
for the download-mode reset, which isn't possible from Docker on macOS. Flash
directly from the host with esptool (`brew install esptool`):

```bash
esptool.py --chip esp32c5 --port /dev/cu.usbmodem* -b 460800 write_flash 0x0 build/firmware.bin
```

Factory-reset flash:

```bash
esptool.py --chip esp32c5 --port /dev/cu.usbmodem* -b 460800 erase_flash && \
esptool.py --chip esp32c5 --port /dev/cu.usbmodem* -b 460800 write_flash 0x0 build/firmware.bin
```

### Monitor

To monitor serial output after flashing:

```bash
idf.py monitor
```

## How it works

### Matter device model

The firmware creates **6 Dimmable Light** endpoints (one per LED). Each
endpoint exposes:

| Cluster             | Purpose                                            |
| ------------------- | -------------------------------------------------- |
| OnOff               | Turn the individual LED on/off                     |
| LevelControl        | Set brightness (0–254)                             |
| ModeSelect          | Selectable role dropdown (6 modes, one per role)   |

Default role assignments (GPIO order):

| GPIO | Mode | Role                   |
| ---- | ---- | ---------------------- |
| 8    | 0    | left front indicator   |
| 1    | 1    | right front indicator  |
| 3    | 2    | left back indicator    |
| 4    | 3    | right back indicator   |
| 5    | 4    | taillight              |
| 6    | 5    | main light             |

### OTA update flow

1. A Matter OTA Provider pushes a newer firmware image to the device.
2. The image is streamed into the inactive flash partition (A/B scheme).
3. After verification, the device reboots into the new firmware.
4. If the new firmware fails to mark itself as valid, the bootloader
   automatically rolls back to the previous version.

### Flash partition layout

```
┌──────────────────────────┐
│  Bootloader              │
├──────────────────────────┤
│  Partition Table         │
├──────────────────────────┤
│  Secure Cert (8 KB)      │  Device attestation
├──────────────────────────┤
│  NVS (48 KB)             │  Matter fabric & config
├──────────────────────────┤
│  NVS Keys (4 KB)         │  NVS encryption keys
├──────────────────────────┤
│  OTA Data (8 KB)         │  Tracks active slot
├──────────────────────────┤
│  PHY Init (4 KB)         │  WiFi/BT calibration
├──────────────────────────┤
│  ota_0 (1920 KB)         │  App slot A
├──────────────────────────┤
│  ota_1 (1920 KB)         │  App slot B
├──────────────────────────┤
│  Factory NVS (24 KB)     │  Factory data
└──────────────────────────┘
```

### CI pipeline

The GitHub Actions workflow (`.github/workflows/ci.yml`):

1. **Release** — bumps version via conventional commits (`release-it`)
2. **Publish builder** — builds and caches the ESP-IDF + ESP-Matter Docker image
3. **Build** — compiles firmware and produces `firmware.bin`
4. **Publish** — uploads `firmware.bin` to GitHub Releases

## License

See [LICENSE](LICENSE).

Default pairing code: 34970112332
