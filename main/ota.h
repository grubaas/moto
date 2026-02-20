#pragma once

#include "esp_err.h"

/// Mark the running firmware as valid, then check for and apply an OTA update.
/// On success the device reboots. On failure returns so the app continues.
esp_err_t ota_check_and_update(void);
