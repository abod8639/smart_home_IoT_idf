#include "firebase_manager.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "ir_manager.h"
#include "gpio_manager.h"
#include "pwm_manager.h"
#include "dht_sensor.h"
#include "wifi_manager.h"
#include "nvs_manager.h"
#include "ws_server.h"
#include "esp_system.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "FIREBASE";

// Internal helper for HTTP REST calls
static esp_err_t firebase_http_request(const char* path, const char* method, const char* post_data, char** response_data) {
    char url[512];
    if (strlen(FIREBASE_AUTH_SECRET) > 0) {
        snprintf(url, sizeof(url), "%s/devices/%s/%s.json?auth=%s", 
                 FIREBASE_BASE_URL, FIREBASE_DEVICE_ID, path, FIREBASE_AUTH_SECRET);
    } else {
        snprintf(url, sizeof(url), "%s/devices/%s/%s.json", 
                 FIREBASE_BASE_URL, FIREBASE_DEVICE_ID, path);
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    if (strcmp(method, "PATCH") == 0) config.method = HTTP_METHOD_PATCH;
    else if (strcmp(method, "PUT") == 0) config.method = HTTP_METHOD_PUT;
    else if (strcmp(method, "DELETE") == 0) config.method = HTTP_METHOD_DELETE;
    else if (strcmp(method, "POST") == 0) config.method = HTTP_METHOD_POST;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    if (post_data) {
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        // ESP_LOGI(TAG, "HTTP %s Status = %d, content_length = %d", method, status_code, content_length);
        
        if (response_data && status_code == 200) {
            // Read response
            if (content_length > 0) {
                *response_data = (char*)malloc(content_length + 1);
                if (*response_data) {
                    int read_len = esp_http_client_read_response(client, *response_data, content_length);
                    (*response_data)[read_len] = '\0';
                }
            } else {
                // Chunked transfer encoding
                int total_read = 0;
                char buffer[128];
                *response_data = (char*)malloc(1);
                (*response_data)[0] = '\0';
                while (1) {
                    int read_len = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
                    if (read_len <= 0) break;
                    buffer[read_len] = '\0';
                    char *new_str = (char*)realloc(*response_data, total_read + read_len + 1);
                    if (new_str) {
                        *response_data = new_str;
                        strcat(*response_data, buffer);
                        total_read += read_len;
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
    return err;
}

esp_err_t firebase_update_status(const char* status) {
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"status\": \"%s\"}", status);
    return firebase_http_request("", "PATCH", payload, NULL);
}

esp_err_t firebase_update_full_state(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    
    cJSON_AddNumberToObject(root, "temperature", dht_sensor_get_temperature());
    cJSON_AddNumberToObject(root, "humidity", dht_sensor_get_humidity());
    cJSON_AddNumberToObject(root, "target_temperature", nvs_get_target_temp(24));
    cJSON_AddNumberToObject(root, "wifi_rssi", wifi_manager_get_rssi());
    cJSON_AddNumberToObject(root, "heap_free", esp_get_free_heap_size());

    cJSON *pins = cJSON_CreateObject();
    if (pins) {
        cJSON_AddNumberToObject(pins, "relay_1", gpio_get_relay_state(RELAY_1_PIN));
        cJSON_AddNumberToObject(pins, "relay_2", gpio_get_relay_state(RELAY_2_PIN));
        cJSON_AddNumberToObject(pins, "relay_3", gpio_get_relay_state(RELAY_3_PIN));
        cJSON_AddNumberToObject(pins, "relay_4", gpio_get_relay_state(RELAY_4_PIN));
        cJSON_AddNumberToObject(pins, "pwm_lamp", pwm_get_duty(PWM_LAMP_PIN));
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

esp_err_t firebase_update_ir_signal(const char* protocol, const char* ir_value_str) {
    char payload[1024]; // Increase size in case of long raw string
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(payload, sizeof(payload), 
             "{\"ir_signal\": {\"protocol\": \"%s\", \"last_value\": \"%s\", \"timestamp\": %ld}}", 
             protocol, ir_value_str, (long)tv.tv_sec);
    return firebase_http_request("", "PATCH", payload, NULL);
}

static void firebase_poll_task(void *pvParameters) {
    ESP_LOGI(TAG, "Firebase poll task started");
    firebase_update_status("online");

    while (1) {
        char *response = NULL;
        esp_err_t err = firebase_http_request("commands", "GET", NULL, &response);
        
        if (err == ESP_OK && response != NULL) {
            // Check if response is not "null"
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
                                    if (count > 0) {
                                        ir_send_raw(durations, count, 38000);
                                    }
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
                                
                                int endpoint = 1;
                                if (pin->valueint == RELAY_2_PIN) endpoint = 2;
                                else if (pin->valueint == RELAY_3_PIN) endpoint = 3;
                                else if (pin->valueint == RELAY_4_PIN) endpoint = 4;
                                
                                char update_buf[128];
                                snprintf(update_buf, sizeof(update_buf), 
                                         "{\"event\": \"relay_update\", \"endpoint\": %d, \"state\": %d}", 
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
                                
                                int endpoint = 5;
                                if (pin->valueint == PWM_RGB_R_PIN || pin->valueint == PWM_RGB_G_PIN || pin->valueint == PWM_RGB_B_PIN) {
                                    endpoint = 6;
                                }
                                
                                char update_buf[128];
                                snprintf(update_buf, sizeof(update_buf), 
                                         "{\"event\": \"pwm_update\", \"endpoint\": %d, \"level\": %d}", 
                                         endpoint, val->valueint);
                                ws_server_broadcast(update_buf);
                                executed = true;
                            }
                        } else if (strcmp(action->valuestring, "control_ac") == 0) {
                            cJSON *is_on = cJSON_GetObjectItem(json, "isOn");
                            cJSON *target_temp = cJSON_GetObjectItem(json, "target_temp");
                            
                            ESP_LOGI(TAG, "\033[1;33m[Firebase Command]\033[0m Action: \033[1;36mControl AC\033[0m ➔ Power: %s, Temp: \033[1;36m%d°C\033[0m", 
                                     (is_on && is_on->valueint) ? "\033[1;32m[ ON ]\033[0m" : "\033[1;31m[ OFF ]\033[0m", 
                                     target_temp ? target_temp->valueint : nvs_get_target_temp(24));
                            if (target_temp) {
                                nvs_save_target_temp(target_temp->valueint);
                            }
                            if (is_on) {
                                gpio_set_relay_state(RELAY_3_PIN, is_on->valueint);
                            }
                            
                            char update_buf[128];
                            snprintf(update_buf, sizeof(update_buf), 
                                     "{\"event\": \"ac_update\", \"isOn\": %s, \"target_temp\": %d}", 
                                     (is_on && is_on->valueint) ? "true" : "false", 
                                     target_temp ? target_temp->valueint : nvs_get_target_temp(24));
                            ws_server_broadcast(update_buf);
                            executed = true;
                        } else if (strcmp(action->valuestring, "ir_learn") == 0) {
                            ESP_LOGI(TAG, "\033[1;33m[Firebase Command]\033[0m Action: \033[1;35mIR Learn\033[0m");
                            ir_manager_start_learning();
                            executed = true;
                        }
                        
                        if (executed) {
                            // Delete command after execution
                            firebase_http_request("commands", "DELETE", NULL, NULL);
                            // Push the newly updated state to Firebase immediately
                            firebase_update_full_state();
                        }
                    }
                    cJSON_Delete(json);
                }
            }
            free(response);
        }
        
        // Poll every 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

esp_err_t firebase_manager_init(void) {
    if (strstr(FIREBASE_BASE_URL, "YOUR-PROJECT") != NULL) {
        ESP_LOGW(TAG, "Firebase not configured! Please set FIREBASE_BASE_URL and FIREBASE_AUTH_SECRET.");
        return ESP_ERR_INVALID_STATE;
    }
    
    xTaskCreate(firebase_poll_task, "firebase_poll_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Firebase manager initialized");
    return ESP_OK;
}
