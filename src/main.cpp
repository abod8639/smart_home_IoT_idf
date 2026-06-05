#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

extern "C" {
#include "nvs_manager.h"
#include "gpio_manager.h"
#include "pwm_manager.h"
#include "ir_manager.h"
#include "dht_sensor.h"
#include "wifi_manager.h"
#include "ws_server.h"
#include "firebase_manager.h"
}
#include "matter_manager.h"

static const char *TAG = "MAIN_APP";

extern "C" void app_main() {
    // Print ASCII Art Logo using direct printf to avoid ESP_LOG prefix
    printf("\033[1;36m"
           "  _____                      _     _    _                      \n"
           " / ____|                    | |   | |  | |                     \n"
           "| (___  _ __ ___   __ _ _ __| |_  | |__| | ___  _ __ ___   ___ \n"
           " \\___ \\| '_ ` _ \\ / _` | '__| __| |  __  |/ _ \\| '_ ` _ \\ / _ \\\n"
           " ____) | | | | | | (_| | |  | |_  | |  | | (_) | | | | | |  __/\n"
           "|_____/|_| |_| |_|\\__,_|_|   \\__| |_|  |_|\\___/|_| |_| |_|\\___|\n"
           "\033[0;33m ============================================================= \033[0m\n\n");

    ESP_LOGI(TAG, "\033[1;32m[SYSTEM]\033[0m Starting Smart Home IoT Application...");

    // 1. Storage
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

    // 3. Network
    ESP_LOGI(TAG, "\033[1;34m[NETWORK]\033[0m Initializing Wi-Fi Manager...");
    wifi_manager_init();

    // 4. WebSocket Server
    ESP_LOGI(TAG, "\033[1;32m[SERVICES]\033[0m Starting WebSocket Server...");
    ws_server_start();

    // 5. Matter Integration
    ESP_LOGI(TAG, "\033[1;32m[SERVICES]\033[0m Initializing Matter Integration...");
    matter_manager_init();

    // 6. Firebase Integration
    ESP_LOGI(TAG, "\033[1;32m[SERVICES]\033[0m Initializing Firebase Integration...");
    firebase_manager_init();

    ESP_LOGI(TAG, "\033[1;32m[SYSTEM]\033[0m Application started successfully.");
    
    // Main loop
    while (true) {
        float temp = 0.0, hum = 0.0;
        if (dht_sensor_read(&temp, &hum) == ESP_OK) {
            ESP_LOGI(TAG, "\033[1;36m[DHT22]\033[0m Climate readout ➔ Temp: \033[1;36m%.1f°C\033[0m | Hum: \033[1;35m%.1f%%\033[0m", temp, hum);
            
            // Broadcast sensor data via WebSockets periodically
            char buf[128];
            snprintf(buf, sizeof(buf), "{\"event\":\"sensor_data\",\"temperature\":%.1f,\"humidity\":%.1f}", temp, hum);
            ws_server_broadcast(buf);

            // Update full state to Firebase
            firebase_update_full_state();
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS); // Update every 10 seconds
    }
}
