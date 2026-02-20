# =============================================================================
# Dockerfile for moto firmware (ESP-IDF / C)
#
#   Uses the official Espressif ESP-IDF Docker image which ships with the
#   full toolchain (CMake, Ninja, riscv32 GCC) for all Espressif SoCs.
# =============================================================================

FROM espressif/idf:v5.5 AS builder

ENV IDF_PATH_FORCE=1

WORKDIR /app

SHELL ["/bin/bash", "-c"]

RUN printf '#!/bin/bash\nsource "$IDF_PATH/export.sh"\nexec "$@"\n' > /entrypoint.sh \
    && chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
