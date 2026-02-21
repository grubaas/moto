#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define LED_MAX_COUNT 6
#define FRAME_MS      8
#define ANIM_COUNT    5

#ifdef __cplusplus
extern "C" {
#endif

/// Initialise LEDC channels for the given GPIO pins (call once at boot).
/// Returns ESP_OK on success.
esp_err_t led_init(const int *gpio_nums, int count);

/// Enable or disable LED output. When off, all channels are driven to 0.
void led_set_power(bool on);

/// Set the global brightness multiplier (Matter LevelControl scale, 0-254).
void led_set_brightness(uint8_t level);

/// Select which animation plays (0 .. ANIM_COUNT-1).
void led_set_animation(int index);

/// FreeRTOS task entry-point — runs the selected animation in a loop.
/// Pass NULL as the argument; this function never returns.
void led_task(void *arg);

#ifdef __cplusplus
}
#endif
