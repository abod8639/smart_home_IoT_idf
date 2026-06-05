#include "ota_manager.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

static const char *TAG = "OTA_MANAGER";

static void ota_task(void *pvParameter) {
    char *url = (char*)pvParameter;
    ESP_LOGI(TAG, "Starting OTA from URL: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Success! Rebooting in 2 seconds...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA Failed!");
    }
    free(url);
    vTaskDelete(NULL);
}

void ota_manager_start(const char *url) {
    char *url_copy = strdup(url);
    if (url_copy) {
        xTaskCreate(&ota_task, "ota_task", 8192, url_copy, 5, NULL);
    } else {
        ESP_LOGE(TAG, "Failed to allocate memory for OTA URL");
    }
}
