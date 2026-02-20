#include "app_driver.h"
#include "breathing.h"

#include <esp_log.h>
#include <esp_matter.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";

static const int s_led_gpios[] = {8, 1, 3, 4, 5, 6};
static const int s_led_count   = sizeof(s_led_gpios) / sizeof(s_led_gpios[0]);

/* ── Matter → hardware helpers ── */

static esp_err_t driver_set_power(esp_matter_attr_val_t *val)
{
    led_set_power(val->val.b);
    return ESP_OK;
}

static esp_err_t driver_set_brightness(esp_matter_attr_val_t *val)
{
    led_set_brightness(val->val.u8);
    return ESP_OK;
}

static esp_err_t driver_set_mode(esp_matter_attr_val_t *val)
{
    led_set_animation(val->val.u8);
    return ESP_OK;
}

/* ── attribute update callback (called by Matter on PRE_UPDATE) ── */

extern uint16_t g_light_endpoint_id;

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id,
                                      uint32_t cluster_id, uint32_t attribute_id,
                                      esp_matter_attr_val_t *val)
{
    if (endpoint_id != g_light_endpoint_id)
        return ESP_OK;

    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        return driver_set_power(val);
    }
    if (cluster_id == LevelControl::Id && attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
        return driver_set_brightness(val);
    }
    if (cluster_id == ModeSelect::Id && attribute_id == ModeSelect::Attributes::CurrentMode::Id) {
        return driver_set_mode(val);
    }

    return ESP_OK;
}

/* ── apply persisted defaults on boot ── */

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    esp_err_t err = ESP_OK;
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    attribute_t *attr = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    attribute::get_val(attr, &val);
    driver_set_power(&val);

    attr = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::get_val(attr, &val);
    driver_set_brightness(&val);

    attr = attribute::get(endpoint_id, ModeSelect::Id, ModeSelect::Attributes::CurrentMode::Id);
    attribute::get_val(attr, &val);
    driver_set_mode(&val);

    return err;
}

/* ── init ── */

app_driver_handle_t app_driver_light_init(void)
{
    esp_err_t err = led_init(s_led_gpios, s_led_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED init failed: %s", esp_err_to_name(err));
    }
    return NULL;
}
