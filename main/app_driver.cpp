#include "app_driver.h"
#include "breathing.h"

#include <esp_log.h>
#include <esp_matter.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";

static const int s_led_gpios[] = {8, 1, 3, 4, 5, 6};

extern uint16_t g_light_endpoint_ids[LED_COUNT];

static int endpoint_to_channel(uint16_t endpoint_id)
{
    for (int i = 0; i < LED_COUNT; i++) {
        if (g_light_endpoint_ids[i] == endpoint_id) return i;
    }
    return -1;
}

/* ── attribute update callback (called by Matter on PRE_UPDATE) ── */

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id,
                                      uint32_t cluster_id, uint32_t attribute_id,
                                      esp_matter_attr_val_t *val)
{
    int ch = endpoint_to_channel(endpoint_id);
    if (ch < 0)
        return ESP_OK;

    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        led_set_channel_power(ch, val->val.b);
        return ESP_OK;
    }
    if (cluster_id == LevelControl::Id && attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
        led_set_channel_brightness(ch, val->val.u8);
        return ESP_OK;
    }

    return ESP_OK;
}

/* ── apply persisted defaults on boot ── */

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    int ch = endpoint_to_channel(endpoint_id);
    if (ch < 0)
        return ESP_ERR_INVALID_ARG;

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    attribute_t *attr = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    attribute::get_val(attr, &val);
    led_set_channel_power(ch, val.val.b);

    attr = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::get_val(attr, &val);
    led_set_channel_brightness(ch, val.val.u8);

    return ESP_OK;
}

/* ── init ── */

app_driver_handle_t app_driver_light_init(void)
{
    esp_err_t err = led_init(s_led_gpios, LED_COUNT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED init failed: %s", esp_err_to_name(err));
    }
    return NULL;
}
