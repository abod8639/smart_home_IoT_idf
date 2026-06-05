#include "nvs_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "NVS_MANAGER";

void nvs_manager_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated and needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS initialized");
}

void nvs_save_pin_state(uint8_t pin, int state) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("pins_state", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "p%d", pin);
        nvs_set_i32(my_handle, key, state);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    } else {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    }
}

int nvs_get_pin_state(uint8_t pin, int default_val) {
    nvs_handle_t my_handle;
    int32_t value = default_val;
    esp_err_t err = nvs_open("pins_state", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "p%d", pin);
        nvs_get_i32(my_handle, key, &value);
        nvs_close(my_handle);
    }
    return value;
}
