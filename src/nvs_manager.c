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

void nvs_save_wifi_credentials(const char* ssid, const char* password) {
    if (!s_handle_open) return;
    nvs_set_str(s_nvs_handle, "wifi_ssid", ssid);
    nvs_set_str(s_nvs_handle, "wifi_pass", password);
    nvs_commit(s_nvs_handle);
}

bool nvs_get_wifi_credentials(char* ssid, size_t ssid_len, char* password, size_t password_len) {
    if (!s_handle_open) return false;
    
    esp_err_t err_ssid = nvs_get_str(s_nvs_handle, "wifi_ssid", ssid, &ssid_len);
    esp_err_t err_pass = nvs_get_str(s_nvs_handle, "wifi_pass", password, &password_len);
    
    return (err_ssid == ESP_OK && err_pass == ESP_OK);
}

// ---------------------------------------------------------------------------
// Matter Endpoint Persistence
// Key scheme (max 15 chars):
//   "mt_cnt"       → int32  total saved device count
//   "mt%d_t" % n  → int32  device_type for slot n
//   "mt%d_p" % n  → int32  pin_num     for slot n
// ---------------------------------------------------------------------------

void nvs_save_matter_device(int slot, int device_type, int pin_num) {
    if (!s_handle_open || slot < 0 || slot >= NVS_MAX_MATTER_DEVICES) return;

    char key_t[12], key_p[12];
    snprintf(key_t, sizeof(key_t), "mt%d_t", slot);
    snprintf(key_p, sizeof(key_p), "mt%d_p", slot);

    nvs_set_i32(s_nvs_handle, key_t, (int32_t)device_type);
    nvs_set_i32(s_nvs_handle, key_p, (int32_t)pin_num);
    nvs_commit(s_nvs_handle);

    ESP_LOGI(TAG, "Matter device saved: slot=%d type=%d pin=%d", slot, device_type, pin_num);
}

bool nvs_get_matter_device(int slot, int *device_type, int *pin_num) {
    if (!s_handle_open || slot < 0 || slot >= NVS_MAX_MATTER_DEVICES) return false;

    char key_t[12], key_p[12];
    snprintf(key_t, sizeof(key_t), "mt%d_t", slot);
    snprintf(key_p, sizeof(key_p), "mt%d_p", slot);

    int32_t dt = 0, pn = 0;
    esp_err_t err_t = nvs_get_i32(s_nvs_handle, key_t, &dt);
    esp_err_t err_p = nvs_get_i32(s_nvs_handle, key_p, &pn);

    if (err_t == ESP_OK && err_p == ESP_OK) {
        *device_type = (int)dt;
        *pin_num     = (int)pn;
        return true;
    }
    return false;
}

int nvs_get_matter_device_count(void) {
    if (!s_handle_open) return 0;
    int32_t count = 0;
    nvs_get_i32(s_nvs_handle, "mt_cnt", &count);
    return (int)count;
}

void nvs_increment_matter_device_count(void) {
    if (!s_handle_open) return;
    int32_t count = 0;
    nvs_get_i32(s_nvs_handle, "mt_cnt", &count);
    count++;
    nvs_set_i32(s_nvs_handle, "mt_cnt", count);
    nvs_commit(s_nvs_handle);
}

void nvs_clear_matter_devices(void) {
    if (!s_handle_open) return;
    int32_t count = 0;
    nvs_get_i32(s_nvs_handle, "mt_cnt", &count);

    for (int i = 0; i < count && i < NVS_MAX_MATTER_DEVICES; i++) {
        char key_t[12], key_p[12];
        snprintf(key_t, sizeof(key_t), "mt%d_t", i);
        snprintf(key_p, sizeof(key_p), "mt%d_p", i);
        nvs_erase_key(s_nvs_handle, key_t);
        nvs_erase_key(s_nvs_handle, key_p);
    }
    nvs_erase_key(s_nvs_handle, "mt_cnt");
    nvs_commit(s_nvs_handle);

    ESP_LOGI(TAG, "All Matter devices cleared from NVS");
}

