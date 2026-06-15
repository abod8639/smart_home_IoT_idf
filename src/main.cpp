#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include <stdio.h>

extern "C" {
#include "device_config.h"
#include "dht_sensor.h"
#include "firebase_manager.h"
#include "gpio_manager.h"
#include "button_manager.h"
#include "ir_manager.h"
#include "mdns_manager.h"
#include "mqtt_manager.h"
#include "nvs_manager.h"
#include "pwm_manager.h"
#include "sntp_manager.h"
#include "wifi_manager.h"
#include "ac_timer_manager.h"
}
#include "matter_manager.h"

static const char *TAG = "MAIN_APP";

// ---------------------------------------------------------------------------
// DHT22 Monitoring Task
// Runs independently so app_main is not blocked by the 20 ms start signal
// or by firebase_update_full_state() network latency.
// ---------------------------------------------------------------------------
static void dht_monitor_task(void *pvParameters) {
    // Wait for WiFi before sending any data
    xEventGroupWaitBits(g_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    // Subscribe to task watchdog
    esp_task_wdt_add(NULL);

    float last_sync_temp = -100.0f;
    float last_sync_hum = -100.0f;
    TickType_t last_sync_time = 0;

    while (true) {
        // Feed the watchdog
        esp_task_wdt_reset();

        float temp = 0.0f, hum = 0.0f;
        esp_err_t err = dht_sensor_read(&temp, &hum);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "\033[1;36m[DHT22]\033[0m Climate readout ➔ "
                          "Temp: \033[1;36m%.1f°C\033[0m | Hum: \033[1;35m%.1f%%\033[0m",
                     temp, hum);

            // Broadcast sensor data via MQTT
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "{\"event\":\"sensor_data\",\"temperature\":%.1f,\"humidity\":%.1f}",
                     temp, hum);
            mqtt_manager_publish_sensor(buf);

            // Sync full device state to Firebase conditionally to save quota
            TickType_t now = xTaskGetTickCount();
            bool time_passed = (now - last_sync_time) > pdMS_TO_TICKS(300000); // 5 mins
            bool temp_changed = (temp - last_sync_temp > 0.5f) || (last_sync_temp - temp > 0.5f);
            bool hum_changed = (hum - last_sync_hum > 2.0f) || (last_sync_hum - hum > 2.0f);

            if (time_passed || temp_changed || hum_changed) {
                firebase_update_full_state();
                last_sync_temp = temp;
                last_sync_hum = hum;
                last_sync_time = now;
            }

        } else if (err == ESP_ERR_INVALID_CRC) {
            ESP_LOGW(TAG, "\033[1;33m[DHT22]\033[0m Checksum error — retrying next cycle");
        } else {
            ESP_LOGW(TAG, "\033[1;33m[DHT22]\033[0m Read timeout — sensor may not be connected");
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 s between reads
    }
}

// ---------------------------------------------------------------------------
// app_main — linear init, then hands off to tasks
// ---------------------------------------------------------------------------
extern "C" void app_main() {
    // ASCII Art banner (direct printf avoids ESP_LOG timestamp prefix)
    printf("\033[1;36m"
           "  _____                      _     _    _                      \n"
           " / ____|                    | |   | |  | |                     \n"
           "| (___  _ __ ___   __ _ _ __| |_  | |__| | ___  _ __ ___   ___ \n"
           " \\___ \\| '_ ` _ \\ / _` | '__| __| |  __  |/ _ \\| '_ ` _ \\ / _ \\\n"
           " ____) | | | | | | (_| | |  | |_  | |  | | (_) | | | | | |  __/\n"
           "|_____/|_| |_| |_|\\__,_|_|   \\__| |_|  |_|\\___/|_| |_| |_|\\___|\n"
           "\033[0;33m ============================================================= \033[0m\n"
           "\033[0;36m  Firmware: %s\033[0m\n\n", FIRMWARE_VERSION);

    ESP_LOGI(TAG, "\033[1;32m[SYSTEM]\033[0m Starting Smart Home IoT Application...");

    // 0. Task Watchdog — monitor critical tasks (30s timeout)
    ESP_LOGI(TAG, "\033[1;31m[WATCHDOG]\033[0m Initializing Task Watchdog (30s timeout)...");
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,       // Don't watch idle tasks
        .trigger_panic = true,     // Reboot on watchdog timeout
    };
    esp_task_wdt_reconfigure(&wdt_config);

    // 1. Storage (must be first — NVS is used by all other managers)
    ESP_LOGI(TAG, "\033[1;35m[STORAGE]\033[0m Initializing NVS Storage...");
    nvs_manager_init();

    // 2. Hardware Peripherals
    ESP_LOGI(TAG, "\033[1;33m[HARDWARE]\033[0m Initializing GPIO Relays...");
    gpio_manager_init();
    ESP_LOGI(TAG, "\033[1;33m[HARDWARE]\033[0m Initializing Lamp and RGB PWM...");
    pwm_manager_init();
    ESP_LOGI(TAG, "\033[1;33m[HARDWARE]\033[0m Initializing IR Manager (RMT)...");
    ir_manager_init();
    ESP_LOGI(TAG, "\033[1;36m[SENSOR]\033[0m Initializing DHT22 Climate Sensor...");
    dht_sensor_init();
    
    ESP_LOGI(TAG, "\033[1;32m[SERVICES]\033[0m Initializing AC Timer...");
    ac_timer_manager_init();

    ESP_LOGI(TAG, "\033[1;33m[HARDWARE]\033[0m Initializing Button Manager...");
    button_manager_init();

    // 3. Network — starts WiFi and creates g_wifi_event_group
    ESP_LOGI(TAG, "\033[1;34m[NETWORK]\033[0m Initializing Wi-Fi Manager...");
    wifi_manager_init();

    // 4. Network-dependent services — start immediately; they wait
    //    internally for WIFI_CONNECTED_BIT before sending anything.
    ESP_LOGI(TAG, "\033[1;32m[SERVICES]\033[0m Starting MQTT Manager...");
    mqtt_manager_init();

    ESP_LOGI(TAG, "\033[1;32m[SERVICES]\033[0m Initializing Matter Integration...");
    matter_manager_init();

    ESP_LOGI(TAG, "\033[1;32m[SERVICES]\033[0m Initializing Firebase Integration...");
    firebase_manager_init();

    // 5. LAN discovery and time sync
    ESP_LOGI(
        TAG,
        "\033[1;34m[NETWORK]\033[0m Initializing mDNS (smarthome.local)...");
    mdns_manager_init();

    ESP_LOGI(TAG, "\033[1;34m[NETWORK]\033[0m Initializing SNTP Time Sync...");
    sntp_manager_init();

    // 6. Sensor monitoring task — waits for WiFi, then runs independently
    ESP_LOGI(TAG, "\033[1;32m[SERVICES]\033[0m Starting DHT22 monitor task...");
    xTaskCreate(dht_monitor_task, "dht_monitor", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "\033[1;32m[SYSTEM]\033[0m All services started. Waiting for WiFi...");

    // app_main task can now be deleted — all work is done in dedicated tasks.
    vTaskDelete(NULL);
}
