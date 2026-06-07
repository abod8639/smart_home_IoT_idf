#include "mdns_manager.h"
#include "device_config.h"
#include "mdns.h"
#include "esp_log.h"

static const char *TAG = "MDNS_MANAGER";

void mdns_manager_init(void) {
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return;
    }

    // Set hostname — device will be reachable as "smarthome.local"
    mdns_hostname_set("smarthome");
    mdns_instance_name_set(DEVICE_NAME);

    // Advertise an MQTT service so Flutter/clients can auto-discover
    mdns_service_add(DEVICE_NAME, "_mqtt", "_tcp", 1883, NULL, 0);

    // Add device metadata as TXT records
    mdns_txt_item_t txt_data[] = {
        {"firmware", FIRMWARE_VERSION},
        {"device_id", DEVICE_ID},
        {"board", "esp32"},
    };
    mdns_service_txt_set("_mqtt", "_tcp", txt_data, sizeof(txt_data) / sizeof(txt_data[0]));

    ESP_LOGI(TAG, "\033[1;34m[mDNS]\033[0m Advertising as \033[1;36msmarthome.local\033[0m");
}
