#include "app_driver.h"
#include "breathing.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <app/server/Server.h>
#include <app/server/CommissioningWindowManager.h>

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static const char *TAG = "app_main";

uint16_t g_light_endpoint_ids[LED_COUNT] = {0};

static constexpr auto k_timeout_seconds = 300;

static const char *s_led_roles[LED_COUNT] = {
    "left front indicator",
    "right front indicator",
    "left back indicator",
    "right back indicator",
    "taillight",
    "main light",
};

/* Vendor-specific cluster carrying the vehicle-light role (read-only string) */
static constexpr uint32_t kRoleClusterId  = 0xFFF10001;
static constexpr uint32_t kRoleAttributeId = 0x0000;

/* ── Matter event callback ── */

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Fabric removed successfully");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            auto &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            constexpr auto kTimeout = chip::System::Clock::Seconds16(k_timeout_seconds);
            if (!commissionMgr.IsCommissioningWindowOpen()) {
                CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(
                    kTimeout, chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR)
                    ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
            }
        }
        break;
    }
    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;
    default:
        break;
    }
}

/* ── Identify callback ── */

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
                                       uint8_t effect_id, uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

/* ── Attribute update callback ── */

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id,
                                         uint32_t cluster_id, uint32_t attribute_id,
                                         esp_matter_attr_val_t *val, void *priv_data)
{
    if (type == PRE_UPDATE) {
        app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
        return app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    }
    return ESP_OK;
}

/* ── Add vendor-specific role cluster to an endpoint ── */

static esp_err_t add_role_cluster(endpoint_t *ep, const char *role)
{
    cluster_t *cl = cluster::create(ep, kRoleClusterId, CLUSTER_FLAG_SERVER);
    if (!cl)
        return ESP_FAIL;

    attribute::create(cl, kRoleAttributeId, ATTRIBUTE_FLAG_WRITABLE | ATTRIBUTE_FLAG_NONVOLATILE,
                      esp_matter_char_str((char *)role, strlen(role)));
    return ESP_OK;
}

/* ── entry point ── */

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* NVS */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* LED hardware */
    app_driver_light_init();

    /* Matter node (root endpoint 0 is created automatically) */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        abort();
    }

    /* Create one dimmable-light endpoint per LED */
    for (int i = 0; i < LED_COUNT; i++) {
        dimmable_light::config_t light_config;
        light_config.on_off.on_off                                  = true;
        light_config.on_off_lighting.start_up_on_off                = nullable<uint8_t>();
        light_config.level_control.current_level                    = nullable<uint8_t>(254);
        light_config.level_control.on_level                         = nullable<uint8_t>(254);
        light_config.level_control_lighting.start_up_current_level  = nullable<uint8_t>(254);

        endpoint_t *endpoint = dimmable_light::create(node, &light_config, ENDPOINT_FLAG_NONE, NULL);
        if (!endpoint) {
            ESP_LOGE(TAG, "Failed to create endpoint for LED %d (%s)", i, s_led_roles[i]);
            abort();
        }

        g_light_endpoint_ids[i] = endpoint::get_id(endpoint);
        ESP_LOGI(TAG, "LED %d (%s) -> endpoint %d", i, s_led_roles[i], g_light_endpoint_ids[i]);

        err = add_role_cluster(endpoint, s_led_roles[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to add role cluster to endpoint %d", g_light_endpoint_ids[i]);
        }

        /* Brightness changes rapidly during transitions — defer NVS writes */
        attribute_t *level_attr = attribute::get(g_light_endpoint_ids[i],
            LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
        attribute::set_deferred_persistence(level_attr);
    }

    /* Start Matter */
    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
        abort();
    }

    /* Disable WiFi power save to keep the Matter session alive */
    esp_wifi_set_ps(WIFI_PS_NONE);

    /* Apply persisted attribute values to the LED driver */
    for (int i = 0; i < LED_COUNT; i++) {
        app_driver_light_set_defaults(g_light_endpoint_ids[i]);
    }

    ESP_LOGI(TAG, "moto Matter device started (%d light endpoints)", LED_COUNT);

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif
}
