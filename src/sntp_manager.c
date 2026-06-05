#include "sntp_manager.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/apps/sntp.h"
#include <time.h>

static const char *TAG = "SNTP_MANAGER";

static void sntp_task(void *pvParameters) {
    // Wait for WiFi connection before starting SNTP
    xEventGroupWaitBits(g_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "Starting SNTP time sync...");

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.google.com");
    sntp_init();

    // Set timezone (Asia/Baghdad = UTC+3, adjust to your location)
    setenv("TZ", "AST-3", 1);
    tzset();

    // Wait for time to be set (timeout after 30 seconds)
    int retry = 0;
    time_t now = 0;
    struct tm timeinfo = {0};
    while (timeinfo.tm_year < (2024 - 1900) && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        localtime_r(&now, &timeinfo);
        retry++;
    }

    if (timeinfo.tm_year >= (2024 - 1900)) {
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGI(TAG, "\033[1;32m[SNTP]\033[0m Time synchronized: %s", strftime_buf);
    } else {
        ESP_LOGW(TAG, "SNTP sync timed out — timestamps may be inaccurate");
    }

    vTaskDelete(NULL);
}

void sntp_manager_init(void) {
    xTaskCreate(sntp_task, "sntp_sync", 3072, NULL, 3, NULL);
}
