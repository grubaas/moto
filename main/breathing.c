#include "breathing.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "breathing";

#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT  /* 0..1023 */
#define LEDC_FREQUENCY  1000               /* 1 kHz */

/* Software fallback: simple on/off blink when LEDC is unavailable */
static void breathing_gpio_fallback(int gpio_num)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);

    ESP_LOGW(TAG, "LEDC unavailable, falling back to GPIO blink on pin %d", gpio_num);

    for (;;) {
        gpio_set_level(gpio_num, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level(gpio_num, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void breathing_run(int gpio_num)
{
    /* Configure LEDC timer */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = LEDC_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed (%s), using GPIO fallback",
                 esp_err_to_name(err));
        breathing_gpio_fallback(gpio_num);
        return;  /* fallback never returns, but just in case */
    }

    /* Configure LEDC channel */
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = gpio_num,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed (%s), using GPIO fallback",
                 esp_err_to_name(err));
        breathing_gpio_fallback(gpio_num);
        return;
    }

    const uint32_t max_duty = (1 << LEDC_DUTY_RES) - 1;
    ESP_LOGI(TAG, "LED PWM on GPIO %d, max duty = %lu", gpio_num, (unsigned long)max_duty);

    /* Breathe forever: ramp up then down */
    for (;;) {
        /* Ramp up: 0 -> BREATHING_STEPS */
        for (int step = 0; step <= BREATHING_STEPS; step++) {
            uint32_t duty = max_duty * step / BREATHING_STEPS;
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            vTaskDelay(pdMS_TO_TICKS(BREATHING_STEP_MS));
        }
        /* Ramp down: BREATHING_STEPS-1 -> 0 */
        for (int step = BREATHING_STEPS - 1; step >= 0; step--) {
            uint32_t duty = max_duty * step / BREATHING_STEPS;
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            vTaskDelay(pdMS_TO_TICKS(BREATHING_STEP_MS));
        }
    }
}
