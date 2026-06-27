#include "command_dispatcher.h"
#include "device_config.h"
#include "gpio_manager.h"
#include "pwm_manager.h"
#include "nvs_manager.h"
#include "ir_manager.h"
#include "ota_manager.h"
#include "mqtt_manager.h"
#include "ac_timer_manager.h"
#include "matter_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CMD_DISPATCH";

// Forward declaration — firebase_manager exposes this for deferred state push.
extern void firebase_trigger_update(void);

// ---------------------------------------------------------------------------
// Internal Helpers
// ---------------------------------------------------------------------------

/**
 * @brief Publish a JSON event string to MQTT (convenience wrapper).
 *        Builds a small JSON object and publishes it, then frees memory.
 */
static void publish_relay_event(int pin, int state) {
    int endpoint = gpio_pin_to_endpoint(pin);
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"event\":\"relay_update\",\"endpoint\":%d,\"state\":%d}",
             endpoint, state);
    mqtt_manager_publish_event(buf);
}

static void publish_pwm_event(int pin, int level) {
    int endpoint = pwm_pin_to_endpoint(pin);
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"event\":\"pwm_update\",\"endpoint\":%d,\"level\":%d}",
             endpoint, level);
    mqtt_manager_publish_event(buf);
}

static void publish_ac_event(bool is_on, int target_temp) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"event\":\"ac_update\",\"isOn\":%s,\"target_temp\":%d}",
             is_on ? "true" : "false", target_temp);
    mqtt_manager_publish_event(buf);
}

// ---------------------------------------------------------------------------
// Command Handlers
// ---------------------------------------------------------------------------

static esp_err_t handle_set_relay(const cJSON *json) {
    cJSON *pin = cJSON_GetObjectItem(json, "pin");
    cJSON *val = cJSON_GetObjectItem(json, "value");
    if (!pin || !val || !cJSON_IsNumber(pin) || !cJSON_IsNumber(val)) {
        ESP_LOGW(TAG, "set_relay: missing or invalid pin/value");
        return ESP_ERR_INVALID_ARG;
    }

    int pin_num = pin->valueint;
    int state   = val->valueint ? 1 : 0;

    if (!is_valid_relay_pin(pin_num)) {
        ESP_LOGW(TAG, "set_relay: invalid pin %d — rejected", pin_num);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "\033[1;36mSet Relay\033[0m ➔ Pin %d = %s",
             pin_num, state ? "\033[1;32m[ ON ]\033[0m" : "\033[1;31m[ OFF ]\033[0m");

    gpio_set_relay_state(pin_num, state);
    publish_relay_event(pin_num, state);
    firebase_trigger_update();
    return ESP_OK;
}

static esp_err_t handle_set_pwm(const cJSON *json) {
    cJSON *pin = cJSON_GetObjectItem(json, "pin");
    cJSON *val = cJSON_GetObjectItem(json, "value");
    if (!pin || !val || !cJSON_IsNumber(pin) || !cJSON_IsNumber(val)) {
        ESP_LOGW(TAG, "set_pwm: missing or invalid pin/value");
        return ESP_ERR_INVALID_ARG;
    }

    int pin_num = pin->valueint;
    int duty    = val->valueint;

    if (!is_valid_pwm_pin(pin_num)) {
        ESP_LOGW(TAG, "set_pwm: invalid pin %d — rejected", pin_num);
        return ESP_ERR_INVALID_ARG;
    }

    // Clamp to valid 8-bit range
    if (duty < 0) duty = 0;
    if (duty > 255) duty = 255;

    ESP_LOGI(TAG, "\033[1;36mSet PWM\033[0m ➔ Pin %d = \033[1;35m%d\033[0m (%d%%)",
             pin_num, duty, (duty * 100) / 255);

    pwm_set_duty(pin_num, (uint32_t)duty);
    publish_pwm_event(pin_num, duty);
    firebase_trigger_update();
    return ESP_OK;
}

static esp_err_t handle_control_ac(const cJSON *json) {
    cJSON *is_on      = cJSON_GetObjectItem(json, "isOn");
    cJSON *target_temp = cJSON_GetObjectItem(json, "target_temp");

    if (is_on && !(cJSON_IsNumber(is_on) || cJSON_IsBool(is_on))) return ESP_ERR_INVALID_ARG;
    if (target_temp && !cJSON_IsNumber(target_temp)) return ESP_ERR_INVALID_ARG;

    int tgt = target_temp ? target_temp->valueint : nvs_get_target_temp(24);

    // Clamp temperature to sane range
    if (tgt < 16) tgt = 16;
    if (tgt > 30) tgt = 30;

    bool power = (is_on && (cJSON_IsTrue(is_on) || (cJSON_IsNumber(is_on) && is_on->valueint != 0)));

    ESP_LOGI(TAG, "\033[1;36mControl AC\033[0m ➔ Power: %s, Temp: \033[1;36m%d°C\033[0m",
             power ? "\033[1;32m[ ON ]\033[0m" : "\033[1;31m[ OFF ]\033[0m", tgt);

    if (target_temp) nvs_save_target_temp(tgt);
    if (is_on) {
        gpio_set_relay_state(RELAY_3_PIN, power ? 1 : 0);
        if (!power) {
            ac_timer_cancel();
        }
    }

    publish_ac_event(power, tgt);
    firebase_trigger_update();
    return ESP_OK;
}

static esp_err_t handle_set_ac_timer(const cJSON *json) {
    cJSON *seconds = cJSON_GetObjectItem(json, "seconds");
    cJSON *ir_code = cJSON_GetObjectItem(json, "ir_code");
    
    if (!seconds || !cJSON_IsNumber(seconds)) {
        ESP_LOGW(TAG, "set_ac_timer: missing or invalid 'seconds'");
        return ESP_ERR_INVALID_ARG;
    }
    
    int secs = seconds->valueint;
    ac_timer_set(secs, ir_code);
    
    firebase_trigger_update();
    return ESP_OK;
}

static esp_err_t handle_ir_send(const cJSON *json) {
    cJSON *protocol = cJSON_GetObjectItem(json, "protocol");
    cJSON *value    = cJSON_GetObjectItem(json, "value");
    cJSON *bits     = cJSON_GetObjectItem(json, "bits");
    cJSON *freq     = cJSON_GetObjectItem(json, "frequency");

    if (!protocol || !value || !cJSON_IsString(protocol) || !cJSON_IsString(value)) {
        ESP_LOGW(TAG, "ir_send: missing or invalid protocol/value");
        return ESP_ERR_INVALID_ARG;
    }
    if (bits && !cJSON_IsNumber(bits)) return ESP_ERR_INVALID_ARG;
    if (freq && !cJSON_IsNumber(freq)) return ESP_ERR_INVALID_ARG;

    if (strcmp(protocol->valuestring, "RAW") == 0 || strcmp(protocol->valuestring, "UNKNOWN") == 0) {
        int count     = bits ? bits->valueint : 256;
        int frequency = freq ? freq->valueint : 38;
        if (count <= 0 || count > 512) count = 256;

        uint16_t *durations = malloc(sizeof(uint16_t) * count);
        if (!durations) return ESP_ERR_NO_MEM;

        char *val_str = strdup(value->valuestring);
        if (!val_str) { free(durations); return ESP_ERR_NO_MEM; }

        char *token = strtok(val_str, ",");
        int   idx   = 0;
        while (token && idx < count) {
            durations[idx++] = (uint16_t)atoi(token);
            token = strtok(NULL, ",");
        }

        ESP_LOGI(TAG, "\033[1;35mIR Send\033[0m ➔ RAW (%d symbols) @ %d kHz", idx, frequency);

        if (idx > 0) {
            ir_send_raw(durations, idx, (uint32_t)frequency * 1000);
        }

        free(val_str);
        free(durations);
    } else {
        // Handle standard protocols (NEC, SAMSUNG, SONY)
        uint32_t data = (uint32_t)strtoul(value->valuestring, NULL, 16);
        int count = bits ? bits->valueint : 32;
        if (count <= 0 || count > 32) count = 32;
        
        int frequency = 38000;
        size_t max_items = (count * 2) + 4;
        uint16_t *durations = malloc(max_items * sizeof(uint16_t));
        if (!durations) return ESP_ERR_NO_MEM;
        
        int idx = 0;

        if (strcmp(protocol->valuestring, "NEC") == 0) {
            durations[idx++] = 9000;
            durations[idx++] = 4500;
            for (int i = 0; i < count; i++) {
                durations[idx++] = 560;
                durations[idx++] = (data & (1UL << i)) ? 1690 : 560;
            }
            durations[idx++] = 560;
        } else if (strcmp(protocol->valuestring, "SAMSUNG") == 0) {
            durations[idx++] = 4500;
            durations[idx++] = 4500;
            for (int i = 0; i < count; i++) {
                durations[idx++] = 560;
                durations[idx++] = (data & (1UL << i)) ? 1690 : 560;
            }
            durations[idx++] = 560;
        } else if (strcmp(protocol->valuestring, "SONY") == 0) {
            frequency = 40000;
            durations[idx++] = 2400;
            durations[idx++] = 600;
            for (int i = 0; i < count; i++) {
                durations[idx++] = (data & (1UL << i)) ? 1200 : 600;
                if (i < count - 1) durations[idx++] = 600;
            }
        } else {
            ESP_LOGW(TAG, "ir_send: unsupported protocol '%s'", protocol->valuestring);
            free(durations);
            return ESP_ERR_NOT_SUPPORTED;
        }
        ESP_LOGI(TAG, "\033[1;35mIR Send\033[0m ➔ %s (0x%lX, %d bits)", protocol->valuestring, (unsigned long)data, count);
        ir_send_raw(durations, idx, frequency);
        free(durations);
    }

    return ESP_OK;
}

static esp_err_t handle_ir_learn(void) {
    ESP_LOGI(TAG, "\033[1;35mIR Learn\033[0m ➔ Starting...");
    ir_manager_start_learning();
    return ESP_OK;
}

static esp_err_t handle_ota_start(const cJSON *json) {
    cJSON *url = cJSON_GetObjectItem(json, "url");
    if (!url || !cJSON_IsString(url) || !url->valuestring[0]) {
        ESP_LOGW(TAG, "ota_start: missing or empty URL");
        return ESP_ERR_INVALID_ARG;
    }

    // Verify HTTPS protocol
    if (strncmp(url->valuestring, "https://", 8) != 0) {
        ESP_LOGE(TAG, "ota_start: insecure URL rejected (must be https://)");
        return ESP_ERR_INVALID_ARG;
    }

#ifdef OTA_TRUSTED_URL_PREFIX
    if (strlen(OTA_TRUSTED_URL_PREFIX) > 0 &&
        strncmp(url->valuestring, OTA_TRUSTED_URL_PREFIX, strlen(OTA_TRUSTED_URL_PREFIX)) != 0) {
        ESP_LOGE(TAG, "ota_start: URL does not match trusted prefix '%s'", OTA_TRUSTED_URL_PREFIX);
        return ESP_ERR_INVALID_ARG;
    }
#endif

    ESP_LOGI(TAG, "\033[1;33mOTA Start\033[0m ➔ %s", url->valuestring);
    ota_manager_start(url->valuestring);
    return ESP_OK;
}

static esp_err_t handle_add_device(const cJSON *json) {
    cJSON *type = cJSON_GetObjectItem(json, "type");
    cJSON *pin = cJSON_GetObjectItem(json, "pin");

    if (!type || !pin || !cJSON_IsNumber(type) || !cJSON_IsNumber(pin)) {
        ESP_LOGW(TAG, "add_device: missing or invalid type/pin");
        return ESP_ERR_INVALID_ARG;
    }

    int device_type = type->valueint;
    int pin_num = pin->valueint;

    ESP_LOGI(TAG, "\033[1;34mAdd Device\033[0m ➔ Type %d on Pin %d", device_type, pin_num);

    // matter_manager_add_endpoint() creates the endpoint AND persists it to
    // NVS so it is restored automatically on the next boot (before start()).
    int endpoint_id = matter_manager_add_endpoint(device_type, pin_num);
    if (endpoint_id >= 0) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "{\"event\":\"device_added\",\"endpoint\":%d,\"type\":%d,\"pin\":%d}",
                 endpoint_id, device_type, pin_num);
        mqtt_manager_publish_event(buf);
        firebase_trigger_update();
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to add Matter endpoint");
        return ESP_FAIL;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t command_dispatcher_execute(const cJSON *json) {
    if (!json) return ESP_ERR_INVALID_ARG;

    cJSON *action = cJSON_GetObjectItem(json, "action");
    if (!action || !cJSON_IsString(action) || !action->valuestring) {
        ESP_LOGW(TAG, "No 'action' field in command");
        return ESP_ERR_INVALID_ARG;
    }

    const char *act = action->valuestring;

    if (strcmp(act, "set_relay")   == 0) return handle_set_relay(json);
    if (strcmp(act, "set_pwm")     == 0) return handle_set_pwm(json);
    if (strcmp(act, "control_ac")  == 0) return handle_control_ac(json);
    if (strcmp(act, "set_ac_timer")== 0) return handle_set_ac_timer(json);
    if (strcmp(act, "ir_send")     == 0) return handle_ir_send(json);
    if (strcmp(act, "send_ir")     == 0) return handle_ir_send(json);  // Firebase alias
    if (strcmp(act, "ir_learn")    == 0) return handle_ir_learn();
    if (strcmp(act, "ota_start")   == 0) return handle_ota_start(json);
    if (strcmp(act, "add_device")  == 0) return handle_add_device(json);
    if (strcmp(act, "get_state")   == 0) return ESP_OK;  // handled by caller

    ESP_LOGW(TAG, "Unknown action: '%s'", act);
    return ESP_ERR_NOT_FOUND;
}
