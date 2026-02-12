# moto

Bare-metal Rust firmware for the **ESP32-C5** microcontroller
(Espressif — RISC-V RV32IMAC), compiled with Docker and simulated in
[Renode](https://renode.io).

## Project structure

```
moto/
├── .cargo/config.toml        # Rust cross-compilation target & flags
├── .github/workflows/
│   └── ci.yml                # GitHub Actions CI — build + Renode test
├── src/
│   └── main.rs               # Bare-metal "Hello, world!" firmware
├── renode/
│   ├── esp32c5.repl          # Renode platform description (RISC-V + UART)
│   ├── esp32c5.resc          # Renode script — local / interactive use
│   └── run-docker.resc       # Renode script — headless Docker execution
├── Cargo.toml                # Rust package manifest
├── build.rs                  # Build script (copies memory.x to OUT_DIR)
├── memory.x                  # Linker script (HP SRAM memory layout)
├── Dockerfile                # Multi-stage: Rust build → Renode runner
├── docker-compose.yml        # Convenience services (firmware, builder)
└── README.md
```

## Quick start (Docker — no local toolchain needed)

```bash
# Build the firmware and run it in the Renode simulator:
docker build -t moto .
docker run --rm moto
```

You should see Renode output containing:

```
Hello, world! Welcome to moto on ESP32-C5 (RISC-V)
```

### Other Docker targets

```bash
# Build only (no simulation):
docker build --target builder -t moto-builder .

# Interactive shell inside the build environment:
docker build --target builder -t moto-builder .
docker run --rm -it moto-builder /bin/bash

# Docker Compose:
docker compose up --build firmware      # build + run
docker compose run --rm builder         # interactive builder shell
```

## Local development

### Prerequisites

1. **Rust** with the RISC-V target:
   ```bash
   rustup target add riscv32imac-unknown-none-elf
   ```
2. **Renode** (for simulation):
   - macOS: `brew install renode`
   - Linux: see https://renode.readthedocs.io/en/latest/introduction/installing.html

### Build & run

```bash
# Compile the firmware:
cargo build --release

# Open Renode with the firmware loaded (interactive GUI):
renode renode/esp32c5.resc
# Then type `start` in the Renode monitor window.
```

## How it works

### Hardware target

The firmware targets the **Espressif ESP32-C5** SoC:

| Feature       | Detail                                     |
|---------------|--------------------------------------------|
| Core          | RISC-V RV32IMAC @ up to 240 MHz           |
| HP SRAM       | 384 KB (unified instruction + data)        |
| LP SRAM       | 16 KB (low-power domain)                   |
| UART          | UART0 @ `0x6000_0000`                     |
| Connectivity  | Wi-Fi 6 (dual-band), BLE 5, 802.15.4      |

### Renode simulation

[Renode](https://renode.io) is an open-source embedded systems simulator by
Antmicro. The platform is defined in `renode/esp32c5.repl` with:

- A RISC-V RV32IMAC CPU
- 384 KB of HP SRAM at the ESP32-C5 address
- An NS16550-compatible UART at the UART0 peripheral address

The firmware writes "Hello, world!" over the UART, which Renode displays on
the console (headless) or in an analyzer window (GUI mode).

### CI pipeline

The GitHub Actions workflow (`.github/workflows/ci.yml`):

1. Builds the firmware for the `riscv32imac-unknown-none-elf` target
2. Runs the firmware in Renode (headless)
3. Asserts that the UART output contains the expected "Hello, world" message

## License

See [LICENSE](LICENSE).
