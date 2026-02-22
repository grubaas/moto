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
#include <app/clusters/mode-select-server/supported-modes-manager.h>

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

/* ── Mode Select delegate: provides the role dropdown options ── */

using ModeOption = ModeSelect::Structs::ModeOptionStruct::Type;

static ModeOption s_role_modes[LED_COUNT] = {
    {chip::CharSpan::fromCharString("left front indicator"),  0, {}},
    {chip::CharSpan::fromCharString("right front indicator"), 1, {}},
    {chip::CharSpan::fromCharString("left back indicator"),   2, {}},
    {chip::CharSpan::fromCharString("right back indicator"),  3, {}},
    {chip::CharSpan::fromCharString("taillight"),             4, {}},
    {chip::CharSpan::fromCharString("main light"),            5, {}},
};

class RoleModesManager : public ModeSelect::SupportedModesManager {
public:
    ModeOptionsProvider getModeOptionsProvider(chip::EndpointId) const override {
        return ModeOptionsProvider(s_role_modes, s_role_modes + LED_COUNT);
    }

    chip::Protocols::InteractionModel::Status
    getModeOptionByMode(chip::EndpointId, uint8_t mode,
                        const ModeOption **dataPtr) const override {
        if (mode < LED_COUNT) {
            *dataPtr = &s_role_modes[mode];
            return chip::Protocols::InteractionModel::Status::Success;
        }
        return chip::Protocols::InteractionModel::Status::InvalidCommand;
    }
};

static RoleModesManager s_role_modes_mgr;

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

    /* Create one dimmable-light endpoint per LED.
       Built manually (instead of dimmable_light::create) to omit the OnOff and
       LevelControl Lighting features, which removes the StartUpOnOff and
       StartUpCurrentLevel attributes and hides the "Power On Behavior" UI in
       Matter controllers. */
    for (int i = 0; i < LED_COUNT; i++) {
        endpoint_t *endpoint = endpoint::create(node, ENDPOINT_FLAG_NONE, NULL);
        if (!endpoint) {
            ESP_LOGE(TAG, "Failed to create endpoint for LED %d (%s)", i, s_led_roles[i]);
            abort();
        }

        cluster::descriptor::config_t desc_config;
        cluster::descriptor::create(endpoint, &desc_config, CLUSTER_FLAG_SERVER);
        add_device_type(endpoint, dimmable_light::get_device_type_id(),
                        dimmable_light::get_device_type_version());

        cluster::identify::config_t id_config;
        id_config.identify_type = chip::to_underlying(Identify::IdentifyTypeEnum::kLightOutput);
        cluster_t *id_cl = cluster::identify::create(endpoint, &id_config, CLUSTER_FLAG_SERVER);
        cluster::identify::command::create_trigger_effect(id_cl);

        cluster::groups::config_t grp_config;
        cluster::groups::create(endpoint, &grp_config, CLUSTER_FLAG_SERVER);

        cluster::on_off::config_t oo_config;
        oo_config.on_off = false;
        cluster_t *oo_cl = cluster::on_off::create(endpoint, &oo_config, CLUSTER_FLAG_SERVER);
        cluster::on_off::command::create_on(oo_cl);
        cluster::on_off::command::create_toggle(oo_cl);

        cluster::level_control::config_t lc_config;
        lc_config.current_level = nullable<uint8_t>(254);
        lc_config.on_level      = nullable<uint8_t>(254);
        cluster_t *lc_cl = cluster::level_control::create(endpoint, &lc_config, CLUSTER_FLAG_SERVER);
        cluster::level_control::feature::on_off::add(lc_cl);
        cluster::level_control::attribute::create_min_level(lc_cl, 1);
        cluster::level_control::attribute::create_max_level(lc_cl, 254);

        cluster::scenes_management::config_t sm_config;
        cluster_t *sm_cl = cluster::scenes_management::create(endpoint, &sm_config, CLUSTER_FLAG_SERVER);
        cluster::scenes_management::command::create_copy_scene(sm_cl);
        cluster::scenes_management::command::create_copy_scene_response(sm_cl);

        /* Mode Select cluster: role selector dropdown */
        cluster::mode_select::config_t ms_config;
        snprintf(ms_config.mode_select_description,
                 sizeof(ms_config.mode_select_description), "Role");
        ms_config.current_mode = static_cast<uint8_t>(i);
        ms_config.delegate     = &s_role_modes_mgr;
        cluster::mode_select::create(endpoint, &ms_config, CLUSTER_FLAG_SERVER);

        g_light_endpoint_ids[i] = endpoint::get_id(endpoint);
        ESP_LOGI(TAG, "LED %d (%s) -> endpoint %d", i, s_led_roles[i], g_light_endpoint_ids[i]);

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
