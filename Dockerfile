# =============================================================================
# Dockerfile for moto Matter firmware (ESP-IDF + ESP-Matter)
#
#   Builds on the official ESP-IDF image, adds the ESP-Matter SDK and its
#   connectedhomeip dependencies for Matter protocol support.
# =============================================================================

FROM espressif/idf:v5.5.1 AS builder

ENV IDF_PATH_FORCE=1

WORKDIR /opt

SHELL ["/bin/bash", "-c"]

# psutil (connectedhomeip dependency) needs Python headers to compile
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3-dev \
    && rm -rf /var/lib/apt/lists/*

# Clone esp-matter (shallow) and bootstrap connectedhomeip submodules.
# This layer is heavy (~2 GB) but cached between builds.
RUN source "$IDF_PATH/export.sh" \
    && git clone --depth 1 https://github.com/espressif/esp-matter.git \
    && cd esp-matter \
    && git submodule update --init --depth 1 \
    && cd connectedhomeip/connectedhomeip \
    && ./scripts/checkout_submodules.py --platform esp32 linux --shallow \
    && cd ../.. \
    && ./install.sh --no-host-tool

ENV ESP_MATTER_PATH=/opt/esp-matter

WORKDIR /app

RUN printf '#!/bin/bash\nsource "$IDF_PATH/export.sh"\nsource "$ESP_MATTER_PATH/export.sh"\nexec "$@"\n' \
    > /entrypoint.sh && chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
