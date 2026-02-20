#include "ota.h"

#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "ota";

#define FIRMWARE_URL "https://github.com/grubaas/moto/releases/latest/download/moto.bin"
#define OTA_BUF_SIZE 4096

esp_err_t ota_check_and_update(void)
{
    /* Mark current firmware as valid to prevent rollback */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Marking running firmware as valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    ESP_LOGI(TAG, "Checking for update at %s", FIRMWARE_URL);

    esp_http_client_config_t http_cfg = {
        .url = FIRMWARE_URL,
        .user_agent = "moto-ota",
        .buffer_size = OTA_BUF_SIZE,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Update applied — rebooting");
        esp_restart();
    }

    if (ret == ESP_ERR_NOT_FOUND || ret == ESP_ERR_HTTP_CONNECT) {
        ESP_LOGI(TAG, "No update available (or server unreachable)");
    } else {
        ESP_LOGW(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }

    return ret;
}
