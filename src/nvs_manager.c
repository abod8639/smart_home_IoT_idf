#include "nvs_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "NVS_MANAGER";

// A single, persistent NVS handle opened once at startup.
// Avoids open/commit/close overhead on every relay toggle (5-10ms per call).
static nvs_handle_t s_nvs_handle = 0;
static bool s_handle_open = false;

void nvs_manager_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated — erasing and re-initializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Open once; keep the handle for the entire application lifetime.
    err = nvs_open("pins_state", NVS_READWRITE, &s_nvs_handle);
    if (err == ESP_OK) {
        s_handle_open = true;
        ESP_LOGI(TAG, "NVS initialized (persistent handle open)");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
    }
}

void nvs_save_pin_state(uint8_t pin, int state) {
    if (!s_handle_open) return;
    char key[8];
    snprintf(key, sizeof(key), "p%d", pin);
    nvs_set_i32(s_nvs_handle, key, state);
    nvs_commit(s_nvs_handle);
}

int nvs_get_pin_state(uint8_t pin, int default_val) {
    if (!s_handle_open) return default_val;
    char key[8];
    int32_t value = default_val;
    snprintf(key, sizeof(key), "p%d", pin);
    nvs_get_i32(s_nvs_handle, key, &value);
    return value;
}

void nvs_save_target_temp(int temp) {
    if (!s_handle_open) return;
    nvs_set_i32(s_nvs_handle, "target_temp", temp);
    nvs_commit(s_nvs_handle);
}

int nvs_get_target_temp(int default_val) {
    if (!s_handle_open) return default_val;
    int32_t value = default_val;
    nvs_get_i32(s_nvs_handle, "target_temp", &value);
    return value;
}
