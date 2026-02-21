#include "breathing.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led";

#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT  /* 0..1023 */
#define LEDC_FREQUENCY  1000               /* 1 kHz */

/* ── controllable state (written from Matter callbacks, read from led_task) ── */

static volatile bool     s_power     = true;
static volatile uint8_t  s_brightness = 254;   /* Matter scale 0-254 */
static volatile int      s_anim      = 0;

static ledc_channel_t s_channels[LED_MAX_COUNT];
static int            s_count   = 0;
static uint32_t       s_max_duty = 0;
static bool           s_inited  = false;

/* ── gamma correction for perceived-linear brightness ── */

static uint32_t gamma_duty(float linear, uint32_t max_duty)
{
    if (linear <= 0.0f) return 0;
    if (linear >= 1.0f) return max_duty;
    float g = linear * linear * linear;   /* γ ≈ 3.0 */
    return (uint32_t)(g * max_duty);
}

/* ── animation helpers (unchanged internally) ── */

static float fabsf_local(float x) { return x < 0.0f ? -x : x; }

static void anim_comet(ledc_channel_t *ch, int n, uint32_t max_duty,
                       float *pos, int *dir)
{
    const float speed    = 0.06f;
    const float tail_len = 2.8f;

    for (int i = 0; i < n; i++) {
        float dist = (float)i - *pos;
        float brightness = 0.0f;

        if (*dir > 0) {
            if (dist >= 0.0f) brightness = 0.0f;
            else              brightness = 1.0f - fabsf_local(dist) / tail_len;
        } else {
            if (dist <= 0.0f) brightness = 0.0f;
            else              brightness = 1.0f - fabsf_local(dist) / tail_len;
        }
        float head = 1.0f - fabsf_local(dist) / 0.6f;
        if (head > brightness) brightness = head;
        if (brightness < 0.0f) brightness = 0.0f;

        ledc_set_duty(LEDC_MODE, ch[i], gamma_duty(brightness, max_duty));
        ledc_update_duty(LEDC_MODE, ch[i]);
    }

    *pos += speed * (*dir);
    if (*pos >= (float)(n - 1)) { *pos = (float)(n - 1); *dir = -1; }
    if (*pos <= 0.0f)           { *pos = 0.0f;           *dir =  1; }
}

static void anim_sparkle(ledc_channel_t *ch, int n, uint32_t max_duty,
                         uint32_t *seed, int frame)
{
    if (frame % 8 != 0) return;

    for (int i = 0; i < n; i++) {
        *seed = *seed * 1103515245u + 12345u;
        float r = (float)((*seed >> 16) & 0x7FFF) / 32767.0f;
        float brightness = (r > 0.7f) ? r : 0.0f;
        ledc_set_duty(LEDC_MODE, ch[i], gamma_duty(brightness, max_duty));
        ledc_update_duty(LEDC_MODE, ch[i]);
    }
}

static void anim_stack(ledc_channel_t *ch, int n, uint32_t max_duty,
                       int *fill, int *filling, int frame)
{
    for (int i = 0; i < n; i++) {
        float brightness = (i < *fill) ? 1.0f : 0.0f;
        ledc_set_duty(LEDC_MODE, ch[i], gamma_duty(brightness, max_duty));
        ledc_update_duty(LEDC_MODE, ch[i]);
    }

    if (frame % 12 != 0) return;

    if (*filling) {
        (*fill)++;
        if (*fill > n) { *fill = n; *filling = 0; }
    } else {
        (*fill)--;
        if (*fill < 0) { *fill = 0; *filling = 1; }
    }
}

static void anim_ping_pong(ledc_channel_t *ch, int n, uint32_t max_duty,
                           float *pos, int *dir)
{
    const float speed = 0.1f;
    const float halo  = 1.4f;

    for (int i = 0; i < n; i++) {
        float dist = fabsf_local((float)i - *pos);
        float brightness = 1.0f - dist / halo;
        if (brightness < 0.0f) brightness = 0.0f;
        ledc_set_duty(LEDC_MODE, ch[i], gamma_duty(brightness, max_duty));
        ledc_update_duty(LEDC_MODE, ch[i]);
    }

    *pos += speed * (*dir);
    if (*pos >= (float)(n - 1)) { *pos = (float)(n - 1); *dir = -1; }
    if (*pos <= 0.0f)           { *pos = 0.0f;           *dir =  1; }
}

static void anim_wave(ledc_channel_t *ch, int n, uint32_t max_duty,
                      float *phase)
{
    const float wave_speed = 0.04f;
    const float wave_len   = 3.0f;

    for (int i = 0; i < n; i++) {
        float v = sinf(*phase + (float)i / wave_len * 2.0f * 3.14159265f);
        float brightness = (v + 1.0f) * 0.5f;
        ledc_set_duty(LEDC_MODE, ch[i], gamma_duty(brightness, max_duty));
        ledc_update_duty(LEDC_MODE, ch[i]);
    }
    *phase += wave_speed;
    if (*phase > 2.0f * 3.14159265f) *phase -= 2.0f * 3.14159265f;
}

/* ── blackout helper ── */

static void all_off(void)
{
    for (int i = 0; i < s_count; i++) {
        ledc_set_duty(LEDC_MODE, s_channels[i], 0);
        ledc_update_duty(LEDC_MODE, s_channels[i]);
    }
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
        s_channels[i] = (ledc_channel_t)(LEDC_CHANNEL_0 + i);
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

void led_set_power(bool on)
{
    s_power = on;
    if (!on) all_off();
}

void led_set_brightness(uint8_t level)
{
    s_brightness = level;
}

void led_set_animation(int index)
{
    if (index >= 0 && index < ANIM_COUNT)
        s_anim = index;
}

void led_task(void *arg)
{
    (void)arg;
    if (!s_inited) {
        ESP_LOGE(TAG, "led_init not called, task exiting");
        vTaskDelete(NULL);
        return;
    }

    int prev_anim = -1;

    float    pos      = 0.0f;
    int      dir      = 1;
    int      fill     = 0, filling = 1;
    float    phase    = 0.0f;
    uint32_t rng_seed = 42u;
    int      frame    = 0;

    for (;;) {
        if (!s_power) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int anim = s_anim;

        /* Reset per-animation state when the mode changes */
        if (anim != prev_anim) {
            pos      = 0.0f;
            dir      = 1;
            fill     = 0;
            filling  = 1;
            phase    = 0.0f;
            rng_seed = 42u + (uint32_t)anim * 7u;
            frame    = 0;
            prev_anim = anim;
            ESP_LOGI(TAG, "animation %d/%d", anim + 1, ANIM_COUNT);
        }

        /* Scale max_duty by the Matter brightness level */
        uint32_t scaled_duty = (s_max_duty * (uint32_t)s_brightness) / 254u;

        switch (anim) {
        case 0: anim_comet(s_channels, s_count, scaled_duty, &pos, &dir);               break;
        case 1: anim_sparkle(s_channels, s_count, scaled_duty, &rng_seed, frame);        break;
        case 2: anim_stack(s_channels, s_count, scaled_duty, &fill, &filling, frame);    break;
        case 3: anim_ping_pong(s_channels, s_count, scaled_duty, &pos, &dir);            break;
        case 4: anim_wave(s_channels, s_count, scaled_duty, &phase);                     break;
        }

        frame++;
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}
