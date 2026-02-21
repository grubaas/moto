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

uint16_t g_light_endpoint_id = 0;

static constexpr auto k_timeout_seconds = 300;

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

/* ── ModeSelect SupportedModesManager ── */

using ModeOption = ModeSelect::Structs::ModeOptionStruct::Type;
using ModeOptionsProvider = ModeSelect::SupportedModesManager::ModeOptionsProvider;

static const char *s_mode_labels[] = {"Comet", "Sparkle", "Stack", "Ping-Pong", "Wave"};
static ModeOption s_mode_options[ANIM_COUNT];

class AnimationModesManager : public ModeSelect::SupportedModesManager {
public:
    AnimationModesManager() {
        for (int i = 0; i < ANIM_COUNT; i++) {
            s_mode_options[i].label = chip::CharSpan::fromCharString(s_mode_labels[i]);
            s_mode_options[i].mode  = static_cast<uint8_t>(i);
        }
    }

    ModeOptionsProvider getModeOptionsProvider(chip::EndpointId endpointId) const override {
        return ModeOptionsProvider(s_mode_options, s_mode_options + ANIM_COUNT);
    }

    chip::Protocols::InteractionModel::Status getModeOptionByMode(
        chip::EndpointId endpointId, uint8_t mode,
        const ModeOption **dataPtr) const override
    {
        if (mode >= ANIM_COUNT)
            return chip::Protocols::InteractionModel::Status::InvalidCommand;
        *dataPtr = &s_mode_options[mode];
        return chip::Protocols::InteractionModel::Status::Success;
    }
};

static AnimationModesManager s_modes_manager;

/* ── Add ModeSelect cluster to the light endpoint ── */

static esp_err_t add_mode_select_cluster(endpoint_t *ep)
{
    cluster::mode_select::config_t ms_config;
    strncpy(ms_config.mode_select_description, "Animation",
            sizeof(ms_config.mode_select_description) - 1);
    ms_config.current_mode = 0;
    ms_config.delegate = &s_modes_manager;

    cluster_t *cluster = cluster::mode_select::create(ep, &ms_config, CLUSTER_FLAG_SERVER);
    if (!cluster) return ESP_FAIL;

    cluster::mode_select::attribute::create_start_up_mode(cluster, nullable<uint8_t>());
    cluster::mode_select::attribute::create_on_mode(cluster, nullable<uint8_t>());

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
    app_driver_handle_t light_handle = app_driver_light_init();

    /* Matter node (root endpoint 0 is created automatically) */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        abort();
    }

    /* Dimmable light endpoint (OnOff + LevelControl) */
    dimmable_light::config_t light_config;
    light_config.on_off.on_off = true;
    light_config.on_off_lighting.start_up_on_off = nullable<uint8_t>();
    light_config.level_control.current_level = nullable<uint8_t>(254);
    light_config.level_control.on_level      = nullable<uint8_t>(254);
    light_config.level_control_lighting.start_up_current_level = nullable<uint8_t>(254);

    endpoint_t *endpoint = dimmable_light::create(node, &light_config, ENDPOINT_FLAG_NONE, light_handle);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to create dimmable light endpoint");
        abort();
    }

    g_light_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Light created with endpoint_id %d", g_light_endpoint_id);

    /* Add Mode Select cluster for animation pattern switching */
    err = add_mode_select_cluster(endpoint);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add ModeSelect cluster: %s", esp_err_to_name(err));
    }

    /* Mark brightness for deferred persistence (changes rapidly during transitions) */
    attribute_t *level_attr = attribute::get(g_light_endpoint_id,
        LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::set_deferred_persistence(level_attr);

    /* Start Matter */
    err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
        abort();
    }

    /* Disable WiFi power save to keep the Matter session alive */
    esp_wifi_set_ps(WIFI_PS_NONE);

    /* Apply persisted attribute values to the LED driver */
    app_driver_light_set_defaults(g_light_endpoint_id);

    /* Start LED animation task */
    xTaskCreate(led_task, "led_anim", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "moto Matter device started");

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif
}
