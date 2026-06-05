#include "firebase_manager.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "ir_manager.h"
#include "gpio_manager.h"
#include "pwm_manager.h"
#include "dht_sensor.h"
#include "nvs_manager.h"
#include "ws_server.h"
#include "esp_system.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "FIREBASE";

/**
 * Mutex protecting firebase_http_request().
 * firebase_update_full_state() is called from two contexts:
 *   1. dht_monitor_task  (main.cpp) — every 10 s
 *   2. firebase_poll_task           — after each command execution
 * Without a mutex, concurrent esp_http_client usage causes heap corruption.
 */
static SemaphoreHandle_t s_http_mutex = NULL;

// ---------------------------------------------------------------------------
// Internal HTTP helper
// ---------------------------------------------------------------------------

static esp_err_t firebase_http_request(const char *path, const char *method,
                                       const char *post_data, char **response_data) {
    // Acquire the mutex — up to 15 s wait to handle slow Firebase responses.
    if (xSemaphoreTake(s_http_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
        ESP_LOGE(TAG, "HTTP mutex timeout — skipping %s %s", method, path);
        return ESP_ERR_TIMEOUT;
    }

    char url[512];
    if (strlen(FIREBASE_AUTH_SECRET) > 0) {
        snprintf(url, sizeof(url), "%s/devices/%s/%s.json?auth=%s",
                 FIREBASE_BASE_URL, FIREBASE_DEVICE_ID, path, FIREBASE_AUTH_SECRET);
    } else {
        snprintf(url, sizeof(url), "%s/devices/%s/%s.json",
                 FIREBASE_BASE_URL, FIREBASE_DEVICE_ID, path);
    }

    esp_http_client_config_t config = {
        .url               = url,
        .method            = HTTP_METHOD_GET,
        .timeout_ms        = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    if      (strcmp(method, "PATCH")  == 0) config.method = HTTP_METHOD_PATCH;
    else if (strcmp(method, "PUT")    == 0) config.method = HTTP_METHOD_PUT;
    else if (strcmp(method, "DELETE") == 0) config.method = HTTP_METHOD_DELETE;
    else if (strcmp(method, "POST")   == 0) config.method = HTTP_METHOD_POST;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        xSemaphoreGive(s_http_mutex);
        return ESP_FAIL;
    }

    if (post_data) {
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code    = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);

        if (response_data && status_code == 200) {
            if (content_length > 0) {
                *response_data = (char *)malloc(content_length + 1);
                if (*response_data) {
                    int read_len = esp_http_client_read_response(client, *response_data, content_length);
                    (*response_data)[read_len] = '\0';
                }
            } else {
                // Chunked transfer encoding
                int   total_read = 0;
                char  buf[128];
                *response_data = (char *)malloc(1);
                (*response_data)[0] = '\0';
                while (1) {
                    int read_len = esp_http_client_read(client, buf, sizeof(buf) - 1);
                    if (read_len <= 0) break;
                    buf[read_len] = '\0';
                    char *new_str = (char *)realloc(*response_data, total_read + read_len + 1);
                    if (new_str) {
                        *response_data = new_str;
                        memcpy(*response_data + total_read, buf, read_len);
                        total_read += read_len;
                        (*response_data)[total_read] = '\0';
                    } else {
                        break;
                    }
                }
            }
        }
    } else {
        ESP_LOGE(TAG, "\033[1;31m[HTTP Failed]\033[0m %s request failed: %s", method, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    xSemaphoreGive(s_http_mutex);
    return err;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t firebase_update_status(const char *status) {
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"status\": \"%s\"}", status);
    return firebase_http_request("", "PATCH", payload, NULL);
}

esp_err_t firebase_update_full_state(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddNumberToObject(root, "temperature",      dht_sensor_get_temperature());
    cJSON_AddNumberToObject(root, "humidity",         dht_sensor_get_humidity());
    cJSON_AddNumberToObject(root, "target_temperature", nvs_get_target_temp(24));
    cJSON_AddNumberToObject(root, "wifi_rssi",        wifi_manager_get_rssi());
    cJSON_AddNumberToObject(root, "heap_free",        esp_get_free_heap_size());

    cJSON *pins = cJSON_CreateObject();
    if (pins) {
        cJSON_AddNumberToObject(pins, "relay_1",   gpio_get_relay_state(RELAY_1_PIN));
        cJSON_AddNumberToObject(pins, "relay_2",   gpio_get_relay_state(RELAY_2_PIN));
        cJSON_AddNumberToObject(pins, "relay_3",   gpio_get_relay_state(RELAY_3_PIN));
        cJSON_AddNumberToObject(pins, "relay_4",   gpio_get_relay_state(RELAY_4_PIN));
        cJSON_AddNumberToObject(pins, "pwm_lamp",  pwm_get_duty(PWM_LAMP_PIN));
        cJSON_AddNumberToObject(pins, "pwm_rgb_r", pwm_get_duty(PWM_RGB_R_PIN));
        cJSON_AddNumberToObject(pins, "pwm_rgb_g", pwm_get_duty(PWM_RGB_G_PIN));
        cJSON_AddNumberToObject(pins, "pwm_rgb_b", pwm_get_duty(PWM_RGB_B_PIN));
        cJSON_AddItemToObject(root, "pins", pins);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    esp_err_t err = firebase_http_request("", "PATCH", json_str, NULL);
    free(json_str);
    return err;
}

esp_err_t firebase_update_ir_signal(const char *protocol, const char *ir_value_str) {
    char payload[1024];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(payload, sizeof(payload),
             "{\"ir_signal\": {\"protocol\": \"%s\", \"last_value\": \"%s\", \"timestamp\": %ld}}",
             protocol, ir_value_str, (long)tv.tv_sec);
    return firebase_http_request("", "PATCH", payload, NULL);
}

// ---------------------------------------------------------------------------
// Poll task
// ---------------------------------------------------------------------------

static void firebase_poll_task(void *pvParameters) {
    // Wait until WiFi has a valid IP before attempting any HTTP request.
    // This eliminates the boot-time flood of ESP_ERR_HTTP_CONNECT errors.
    ESP_LOGI(TAG, "Firebase poll task waiting for WiFi...");
    xEventGroupWaitBits(g_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "Firebase poll task started");
    firebase_update_status("online");

    while (1) {
        // Only poll when connected
        EventBits_t bits = xEventGroupGetBits(g_wifi_event_group);
        if (!(bits & WIFI_CONNECTED_BIT)) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        char *response = NULL;
        esp_err_t err = firebase_http_request("commands", "GET", NULL, &response);

        if (err == ESP_OK && response != NULL) {
            if (strcmp(response, "null") != 0) {
                cJSON *json = cJSON_Parse(response);
                if (json != NULL) {
                    cJSON *action = cJSON_GetObjectItem(json, "action");
                    if (action && cJSON_IsString(action)) {
                        bool executed = false;

                        if (strcmp(action->valuestring, "send_ir") == 0) {
                            cJSON *value = cJSON_GetObjectItem(json, "value");
                            if (value && cJSON_IsString(value)) {
                                ESP_LOGI(TAG, "\033[1;33m[Firebase Command]\033[0m Action: \033[1;35mSend IR\033[0m");
                                char *val_copy = strdup(value->valuestring);
                                if (val_copy) {
                                    uint16_t *durations = malloc(sizeof(uint16_t) * 256);
                                    size_t count = 0;
                                    char *token = strtok(val_copy, ",");
                                    while (token != NULL && count < 256) {
                                        durations[count++] = (uint16_t)atoi(token);
                                        token = strtok(NULL, ",");
                                    }
                                    if (count > 0) ir_send_raw(durations, count, 38000);
                                    free(durations);
                                    free(val_copy);
                                    executed = true;
                                }
                            }

                        } else if (strcmp(action->valuestring, "set_relay") == 0) {
                            cJSON *pin = cJSON_GetObjectItem(json, "pin");
                            cJSON *val = cJSON_GetObjectItem(json, "value");
                            if (pin && val) {
                                ESP_LOGI(TAG, "\033[1;33m[Firebase Command]\033[0m Action: \033[1;36mSet Relay\033[0m ➔ Pin %d = %s",
                                         pin->valueint, val->valueint ? "\033[1;32m[ ON ]\033[0m" : "\033[1;31m[ OFF ]\033[0m");
                                gpio_set_relay_state(pin->valueint, val->valueint);

                                int endpoint = gpio_pin_to_endpoint(pin->valueint);
                                char update_buf[96];
                                snprintf(update_buf, sizeof(update_buf),
                                         "{\"event\":\"relay_update\",\"endpoint\":%d,\"state\":%d}",
                                         endpoint, val->valueint);
                                ws_server_broadcast(update_buf);
                                executed = true;
                            }

                        } else if (strcmp(action->valuestring, "set_pwm") == 0) {
                            cJSON *pin = cJSON_GetObjectItem(json, "pin");
                            cJSON *val = cJSON_GetObjectItem(json, "value");
                            if (pin && val) {
                                ESP_LOGI(TAG, "\033[1;33m[Firebase Command]\033[0m Action: \033[1;36mSet PWM\033[0m ➔ Pin %d = \033[1;35m%d\033[0m (%d%%)",
                                         pin->valueint, val->valueint, (val->valueint * 100) / 255);
                                pwm_set_duty(pin->valueint, val->valueint);

                                int endpoint = (pin->valueint == PWM_RGB_R_PIN ||
                                                pin->valueint == PWM_RGB_G_PIN ||
                                                pin->valueint == PWM_RGB_B_PIN) ? 6 : 5;
                                char update_buf[96];
                                snprintf(update_buf, sizeof(update_buf),
                                         "{\"event\":\"pwm_update\",\"endpoint\":%d,\"level\":%d}",
                                         endpoint, val->valueint);
                                ws_server_broadcast(update_buf);
                                executed = true;
                            }

                        } else if (strcmp(action->valuestring, "control_ac") == 0) {
                            cJSON *is_on      = cJSON_GetObjectItem(json, "isOn");
                            cJSON *target_temp = cJSON_GetObjectItem(json, "target_temp");
                            int   tgt          = target_temp ? target_temp->valueint : nvs_get_target_temp(24);

                            ESP_LOGI(TAG, "\033[1;33m[Firebase Command]\033[0m Action: \033[1;36mControl AC\033[0m ➔ Power: %s, Temp: \033[1;36m%d°C\033[0m",
                                     (is_on && is_on->valueint) ? "\033[1;32m[ ON ]\033[0m" : "\033[1;31m[ OFF ]\033[0m", tgt);

                            if (target_temp) nvs_save_target_temp(tgt);
                            if (is_on) gpio_set_relay_state(RELAY_3_PIN, is_on->valueint);

                            char update_buf[96];
                            snprintf(update_buf, sizeof(update_buf),
                                     "{\"event\":\"ac_update\",\"isOn\":%s,\"target_temp\":%d}",
                                     (is_on && is_on->valueint) ? "true" : "false", tgt);
                            ws_server_broadcast(update_buf);
                            executed = true;

                        } else if (strcmp(action->valuestring, "ir_learn") == 0) {
                            ESP_LOGI(TAG, "\033[1;33m[Firebase Command]\033[0m Action: \033[1;35mIR Learn\033[0m");
                            ir_manager_start_learning();
                            executed = true;
                        }

                        if (executed) {
                            firebase_http_request("commands", "DELETE", NULL, NULL);
                            firebase_update_full_state();
                        }
                    }
                    cJSON_Delete(json);
                }
            }
            free(response);
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

esp_err_t firebase_manager_init(void) {
    if (strstr(FIREBASE_BASE_URL, "YOUR-PROJECT") != NULL) {
        ESP_LOGW(TAG, "Firebase not configured! Set FIREBASE_BASE_URL in firebase_manager.h.");
        return ESP_ERR_INVALID_STATE;
    }

    // Create the HTTP mutex before spawning the poll task.
    s_http_mutex = xSemaphoreCreateMutex();
    if (!s_http_mutex) {
        ESP_LOGE(TAG, "Failed to create HTTP mutex");
        return ESP_ERR_NO_MEM;
    }

    xTaskCreate(firebase_poll_task, "firebase_poll", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Firebase manager initialized");
    return ESP_OK;
}
