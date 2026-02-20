#include "breathing.h"
#include "ota.h"
#include "wifi.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "moto";

#define LED_GPIO 8

/* Critical — starts instantly, runs forever */
static void breathing_task(void *arg)
{
    breathing_run(LED_GPIO);
}

/* Non-critical — can fail without affecting the LED */
static void network_task(void *arg)
{
    esp_err_t err = wifi_connect();
    if (err == ESP_OK) {
        ota_check_and_update();
    } else {
        ESP_LOGW(TAG, "WiFi failed, skipping OTA");
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Initialise NVS — required by WiFi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "moto ESP32-C5 firmware");

    /* Start breathing LED immediately — this is critical, never blocked */
    xTaskCreate(breathing_task, "breathing", 2048, NULL, 5, NULL);

    /* Start WiFi+OTA in a separate lower-priority task */
    xTaskCreate(network_task, "network", 4096, NULL, 3, NULL);
}
