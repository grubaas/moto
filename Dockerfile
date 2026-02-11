# =============================================================================
# Multi-stage Dockerfile for moto firmware
#
#   Stage 1 (builder) — compiles Rust firmware for thumbv7em-none-eabihf
#   Stage 2 (runner)  — runs the ELF in the Renode simulator (headless)
# =============================================================================

# ---- Build Stage ------------------------------------------------------------
FROM rust:1-bookworm AS builder

# Cross-compilation target for Cortex-M7 with hardware FPU
RUN rustup target add thumbv7em-none-eabihf

WORKDIR /app

# 1. Copy dependency manifests & build config first (Docker layer caching)
COPY Cargo.toml ./
COPY .cargo .cargo
COPY build.rs memory.x ./

# 2. Create a minimal dummy source so `cargo build` downloads & compiles deps
RUN mkdir src && \
    printf '#![no_std]\n#![no_main]\nuse panic_halt as _;\n#[cortex_m_rt::entry]\nfn main() -> ! { loop {} }\n' \
    > src/main.rs

RUN cargo build --release

# 3. Replace dummy source with the real firmware and rebuild
COPY src/ src/
RUN touch src/main.rs && cargo build --release

# ---- Renode Stage -----------------------------------------------------------
# No official arm64 Docker image exists for Renode, so we build our own from
# the portable .NET tarball published at https://builds.renode.io/.
# TARGETARCH is set automatically by Docker (amd64 | arm64).
FROM debian:bookworm-slim AS runner

ARG TARGETARCH

RUN apt-get update && apt-get install -y --no-install-recommends \
        libicu72 libssl3 ca-certificates wget \
    && rm -rf /var/lib/apt/lists/*

# Download the architecture-appropriate Renode portable .NET build
RUN if [ "$TARGETARCH" = "arm64" ]; then \
      RENODE_URL="https://builds.renode.io/renode-latest.linux-arm64-portable-dotnet.tar.gz"; \
    else \
      RENODE_URL="https://builds.renode.io/renode-latest.linux-portable-dotnet.tar.gz"; \
    fi && \
    wget -qO /tmp/renode.tar.gz "$RENODE_URL" && \
    mkdir -p /opt/renode && \
    tar -xzf /tmp/renode.tar.gz -C /opt/renode --strip-components=1 && \
    rm /tmp/renode.tar.gz

ENV PATH="/opt/renode:${PATH}"

WORKDIR /app

# Copy the compiled firmware ELF
COPY --from=builder /app/target/thumbv7em-none-eabihf/release/moto /app/firmware.elf

# Copy Renode platform description & scripts
COPY renode/ /app/renode/

# Default: run firmware headlessly and print UART output to the console
ENTRYPOINT ["renode", "--disable-xwt", "--console"]
CMD ["-e", "include @/app/renode/run-docker.resc"]
