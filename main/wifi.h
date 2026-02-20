#pragma once

#include "esp_err.h"

/// Connect to WiFi in station mode using compile-time credentials.
/// Blocks until an IP address is obtained or an error occurs.
esp_err_t wifi_connect(void);
