#include "breathing.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "led";

#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT  /* 0..1023 */
#define LEDC_FREQUENCY  1000               /* 1 kHz */

static ledc_channel_t s_channels[LED_MAX_COUNT];
static int            s_count    = 0;
static uint32_t       s_max_duty = 0;
static bool           s_inited   = false;

static bool    s_power[LED_MAX_COUNT];
static uint8_t s_brightness[LED_MAX_COUNT];

/* ── gamma correction for perceived-linear brightness (γ ≈ 3.0) ── */

static uint32_t gamma_duty(float linear, uint32_t max_duty)
{
    if (linear <= 0.0f) return 0;
    if (linear >= 1.0f) return max_duty;
    float g = linear * linear * linear;
    return (uint32_t)(g * max_duty);
}

static void update_channel(int ch)
{
    uint32_t duty = 0;
    if (s_power[ch]) {
        float level = (float)s_brightness[ch] / 254.0f;
        duty = gamma_duty(level, s_max_duty);
    }
    ledc_set_duty(LEDC_MODE, s_channels[ch], duty);
    ledc_update_duty(LEDC_MODE, s_channels[ch]);
}

/* ── public API ── */

esp_err_t led_init(const int *gpio_nums, int count)
{
    if (count <= 0) return ESP_ERR_INVALID_ARG;
    if (count > LED_MAX_COUNT) count = LED_MAX_COUNT;

    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = LEDC_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) return err;

    int active = 0;
    for (int i = 0; i < count; i++) {
        s_channels[i]   = (ledc_channel_t)(LEDC_CHANNEL_0 + i);
        s_power[i]      = true;
        s_brightness[i] = 254;

        ledc_channel_config_t ch_cfg = {
            .gpio_num   = gpio_nums[i],
            .speed_mode = LEDC_MODE,
            .channel    = s_channels[i],
            .timer_sel  = LEDC_TIMER,
            .duty       = 0,
            .hpoint     = 0,
        };
        if (ledc_channel_config(&ch_cfg) != ESP_OK) {
            ESP_LOGE(TAG, "channel config failed for GPIO %d, skipping", gpio_nums[i]);
            continue;
        }
        ESP_LOGI(TAG, "PWM on GPIO %d (ch %d)", gpio_nums[i], s_channels[i]);
        active++;
    }
    if (active == 0) return ESP_FAIL;

    s_count    = count;
    s_max_duty = (1 << LEDC_DUTY_RES) - 1;
    s_inited   = true;
    return ESP_OK;
}

void led_set_channel_power(int channel, bool on)
{
    if (!s_inited || channel < 0 || channel >= s_count) return;
    s_power[channel] = on;
    update_channel(channel);
}

void led_set_channel_brightness(int channel, uint8_t level)
{
    if (!s_inited || channel < 0 || channel >= s_count) return;
    s_brightness[channel] = level;
    update_channel(channel);
}
