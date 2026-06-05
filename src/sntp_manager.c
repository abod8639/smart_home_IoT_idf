#include "sntp_manager.h"
#include "wifi_manager.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>

static const char *TAG = "SNTP_MANAGER";

static void time_sync_notification_cb(struct timeval *tv) {
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "\033[1;32m[SNTP]\033[0m Time synchronized: %s", strftime_buf);
}

static void sntp_task(void *pvParameters) {
    // Wait for WiFi connection before starting SNTP
    xEventGroupWaitBits(g_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "Starting SNTP time sync...");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    // Set timezone (Asia/Baghdad = UTC+3, adjust to your location)
    setenv("TZ", "AST-3", 1);
    tzset();

    // Wait for time to be set (timeout after 30 seconds)
    int retry = 0;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (retry >= 30) {
        ESP_LOGW(TAG, "SNTP sync timed out — timestamps may be inaccurate");
    }

    vTaskDelete(NULL);
}

void sntp_manager_init(void) {
    xTaskCreate(sntp_task, "sntp_sync", 3072, NULL, 3, NULL);
}
