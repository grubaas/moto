#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define LED_MAX_COUNT 6

#ifdef __cplusplus
extern "C" {
#endif

/// Initialise LEDC channels for the given GPIO pins (call once at boot).
esp_err_t led_init(const int *gpio_nums, int count);

/// Turn a single LED channel on or off. Immediately updates the PWM output.
void led_set_channel_power(int channel, bool on);

/// Set brightness for a single LED channel (Matter LevelControl scale, 0-254).
void led_set_channel_brightness(int channel, uint8_t level);

#ifdef __cplusplus
}
#endif
