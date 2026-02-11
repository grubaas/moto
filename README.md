# moto

Bare-metal Rust firmware for a **Teensy 4.1**-class microcontroller
(NXP i.MX RT1062 — ARM Cortex-M7), compiled with Docker and simulated in
[Renode](https://renode.io).

## Project structure

```
moto/
├── .cargo/config.toml        # Rust cross-compilation target & flags
├── .github/workflows/
│   └── build.yml             # GitHub Actions CI — build + Renode test
├── src/
│   └── main.rs               # Bare-metal "Hello, world!" firmware
├── renode/
│   ├── teensy41.repl         # Renode platform description (Cortex-M7 + UART)
│   ├── teensy41.resc         # Renode script — local / interactive use
│   └── run-docker.resc       # Renode script — headless Docker execution
├── Cargo.toml                # Rust package manifest
├── build.rs                  # Build script (copies memory.x to OUT_DIR)
├── memory.x                  # Linker script (ITCM + DTCM memory layout)
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
Hello, world! Welcome to moto on Teensy 4.1 (i.MX RT1062)
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

1. **Rust** with the Cortex-M7 target:
   ```bash
   rustup target add thumbv7em-none-eabihf
   ```
2. **Renode** (for simulation):
   - macOS: `brew install renode`
   - Linux: see https://renode.readthedocs.io/en/latest/introduction/installing.html

### Build & run

```bash
# Compile the firmware:
cargo build --release

# Open Renode with the firmware loaded (interactive GUI):
renode renode/teensy41.resc
# Then type `start` in the Renode monitor window.
```

## How it works

### Hardware target

The firmware targets the **NXP i.MX RT1062** SoC found on the Teensy 4.1:

| Feature       | Detail                           |
|---------------|----------------------------------|
| Core          | ARM Cortex-M7 @ 600 MHz         |
| ITCM          | 512 KB (code execution)          |
| DTCM          | 512 KB (stack + data)            |
| OCRAM         | 1 MB (extra SRAM)               |
| UART          | LPUART1 @ `0x4018_4000`         |

### Renode simulation

[Renode](https://renode.io) is an open-source embedded systems simulator by
Antmicro. The platform is defined in `renode/teensy41.repl` with:

- A Cortex-M7 CPU
- Memory regions matching the i.MX RT1062 (ITCM, DTCM, OCRAM)
- An NS16550-compatible UART at the LPUART1 address

The firmware writes "Hello, world!" over the UART, which Renode displays on
the console (headless) or in an analyzer window (GUI mode).

### CI pipeline

The GitHub Actions workflow (`.github/workflows/build.yml`):

1. Builds the Docker image (compiles the firmware inside the container)
2. Runs the firmware in Renode (headless)
3. Asserts that the UART output contains the expected "Hello, world" message

## License

See [LICENSE](LICENSE).
