#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "nvs_manager.h"
#include "captive_portal.h"
#include <string.h>

static const char *TAG = "WIFI_MANAGER";

// Global event group — set WIFI_CONNECTED_BIT when an IP is obtained.
EventGroupHandle_t g_wifi_event_group = NULL;

static int s_retry_num = 0;
static TimerHandle_t s_reconnect_timer = NULL;

static void wifi_reconnect_callback(TimerHandle_t xTimer) {
    ESP_LOGI(TAG, "\033[1;36m[WIFI]\033[0m Timer triggered — executing auto-reconnect...");
    esp_wifi_connect();
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Clear the connected bit immediately so waiters stop.
        xEventGroupClearBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
        s_retry_num++;
        ESP_LOGW(TAG, "\033[1;33m[WIFI]\033[0m Disconnected — retry #%d (scheduling auto-reconnect in 5s)...", s_retry_num);
        if (s_reconnect_timer) {
            xTimerStart(s_reconnect_timer, 0);
        } else {
            esp_wifi_connect(); // fallback if timer creation failed
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "\033[1;32m[WIFI]\033[0m Connected ✓ — IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        if (s_reconnect_timer) {
            xTimerStop(s_reconnect_timer, 0);
        }
        // Signal all waiters (WebSocket server, Firebase, etc.)
        xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_manager_init(void) {
    // Create the event group before starting WiFi so handlers can set bits.
    g_wifi_event_group = xEventGroupCreate();

    // Create the software timer for reconnect backoff/delay (5 seconds, one-shot)
    s_reconnect_timer = xTimerCreate("wifi_recon_timer", pdMS_TO_TICKS(5000), pdFALSE, NULL, wifi_reconnect_callback);
    if (!s_reconnect_timer) {
        ESP_LOGE(TAG, "Failed to create WiFi reconnect timer!");
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // Try to load credentials from NVS
    char ssid[32] = {0};
    char pass[64] = {0};
    
    if (nvs_get_wifi_credentials(ssid, sizeof(ssid), pass, sizeof(pass)) && strlen(ssid) > 0) {
        ESP_LOGI(TAG, "Loaded WiFi credentials from NVS. Connecting to %s...", ssid);
        
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        ESP_LOGW(TAG, "No WiFi credentials found in NVS. Starting Captive Portal...");
        wifi_manager_start_captive_portal();
    }
}

void wifi_manager_start_captive_portal(void) {
    ESP_LOGI(TAG, "Starting Wi-Fi Access Point (SmartHome-Setup)");
    
    // Create AP netif if not exists
    esp_netif_create_default_wifi_ap();
    
    // Stop WiFi if it was already running in STA mode
    esp_wifi_stop();
    
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "SmartHome-Setup",
            .ssid_len = strlen("SmartHome-Setup"),
            .channel = 1,
            .password = "", // Open network
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA)); // APSTA to allow scanning while broadcasting
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Start the Captive Portal HTTP Server and DNS Server
    captive_portal_start();
}

int wifi_manager_get_rssi(void) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return -100;
}
