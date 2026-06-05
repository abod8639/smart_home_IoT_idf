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
    ESP_LOGI(TAG, "Starting Smart Home IoT Application...");

    // 1. Storage
    nvs_manager_init();

    // 2. Hardware Peripherals
    gpio_manager_init();
    pwm_manager_init();
    ir_manager_init();
    dht_sensor_init();

    // 3. Network
    wifi_manager_init();

    // 4. WebSocket Server
    // Note: It's usually better to wait for WiFi IP before starting server,
    // but the esp_http_server can be started and it will bind when interface is up.
    ws_server_start();

    // 5. Matter Integration
    matter_manager_init();

    // 6. Firebase Integration
    firebase_manager_init();

    ESP_LOGI(TAG, "Application started successfully.");
    
    // Main loop
    while (true) {
        float temp = 0.0, hum = 0.0;
        if (dht_sensor_read(&temp, &hum) == ESP_OK) {
            ESP_LOGI(TAG, "DHT22: Temp=%.1fC, Hum=%.1f%%", temp, hum);
            
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
