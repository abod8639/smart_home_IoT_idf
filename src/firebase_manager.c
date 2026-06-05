#include "firebase_manager.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "ir_manager.h"
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
        // .cert_pem = (const char *)firebase_cert_pem_start, // Add SSL cert here for production
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
        ESP_LOGE(TAG, "HTTP %s request failed: %s", method, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t firebase_update_status(const char* status) {
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"status\": \"%s\"}", status);
    return firebase_http_request("", "PATCH", payload, NULL);
}

esp_err_t firebase_update_ir_signal(const char* protocol, uint32_t ir_value) {
    char payload[128];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(payload, sizeof(payload), 
             "{\"ir_signal\": {\"protocol\": \"%s\", \"last_value\": \"0x%X\", \"timestamp\": %ld}}", 
             protocol, (unsigned int)ir_value, (long)tv.tv_sec);
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
                    cJSON *value = cJSON_GetObjectItem(json, "value");
                    
                    if (action && cJSON_IsString(action) && strcmp(action->valuestring, "send_ir") == 0) {
                        if (value && cJSON_IsString(value)) {
                            // Convert string hex to uint32
                            uint32_t ir_code = (uint32_t)strtol(value->valuestring, NULL, 16);
                            ESP_LOGI(TAG, "Executing IR Command from Firebase: 0x%X", (unsigned int)ir_code);
                            // Assuming protocol NEC as default or fetch from JSON
                            ir_manager_send("NEC", ir_code);
                            
                            // Delete command after execution
                            firebase_http_request("commands", "DELETE", NULL, NULL);
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
