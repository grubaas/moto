#pragma once

#define BREATHING_STEPS 100
#define BREATHING_STEP_MS 20

/// Run the breathing LED loop forever on the given GPIO pin.
/// This function never returns.
void breathing_run(int gpio_num);
