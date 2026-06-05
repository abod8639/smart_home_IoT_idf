#include "wifi_manager.h"
#include "wifi_credentials.h"  // gitignored — edit WIFI_SSID / WIFI_PASSWORD there
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WIFI_MANAGER";

// Global event group — set WIFI_CONNECTED_BIT when an IP is obtained.
EventGroupHandle_t g_wifi_event_group = NULL;

static int s_retry_num = 0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Clear the connected bit immediately so waiters stop.
        xEventGroupClearBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
        s_retry_num++;
        ESP_LOGW(TAG, "\033[1;33m[WIFI]\033[0m Disconnected — retry #%d (auto-reconnect)...", s_retry_num);
        // Always retry — an IoT device should never give up reconnecting.
        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "\033[1;32m[WIFI]\033[0m Connected ✓ — IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        // Signal all waiters (WebSocket server, Firebase, etc.)
        xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_manager_init(void) {
    // Create the event group before starting WiFi so handlers can set bits.
    g_wifi_event_group = xEventGroupCreate();

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

    // Credentials are defined in src/wifi_credentials.h (gitignored).
    // Edit that file to set your SSID/password — it is never committed.
    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

int wifi_manager_get_rssi(void) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return -100;
}
