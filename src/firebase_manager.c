#include "firebase_manager.h"
#include "cJSON.h"
#include "command_dispatcher.h"
#include "device_config.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "matter_manager.h"
#include "state_builder.h"
#include "wifi_manager.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "FIREBASE";

// Event group bit used as a thread-safe flag for deferred state updates.
// Replaces the old `volatile bool g_firebase_needs_update`.
#define FIREBASE_UPDATE_BIT BIT0
static EventGroupHandle_t s_firebase_event_group = NULL;

/**
 * Mutex protecting firebase_http_request().
 * firebase_update_full_state() is called from two contexts:
 *   1. dht_monitor_task  (main.cpp) — every 10 s
 *   2. firebase_poll_task           — after each command execution
 * Without a mutex, concurrent esp_http_client usage causes heap corruption.
 */
static SemaphoreHandle_t s_http_mutex = NULL;

struct http_response_buffer {
  char *data;
  int len;
  int capacity;
};

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    struct http_response_buffer *res =
        (struct http_response_buffer *)evt->user_data;
    if (res && res->data) {
      int needed = res->len + evt->data_len + 1;
      if (needed > res->capacity) {
        int new_cap = needed * 2;
        char *new_data = realloc(res->data, new_cap);
        if (new_data) {
          res->data = new_data;
          res->capacity = new_cap;
        } else {
          return ESP_ERR_NO_MEM;
        }
      }
      memcpy(res->data + res->len, evt->data, evt->data_len);
      res->len += evt->data_len;
      res->data[res->len] = '\0';
    }
  }
  return ESP_OK;
}

static esp_err_t firebase_http_request(const char *path, const char *method,
                                       const char *post_data,
                                       char **response_data) {
  // Acquire the mutex — up to 15 s wait to handle slow Firebase responses.
  if (xSemaphoreTake(s_http_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
    ESP_LOGE(TAG, "HTTP mutex timeout — skipping %s %s", method, path);
    return ESP_ERR_TIMEOUT;
  }

  char url[512];
  if (strlen(FIREBASE_AUTH_SECRET) > 0) {
    snprintf(url, sizeof(url), "%s/devices/%s/%s.json?auth=%s",
             FIREBASE_BASE_URL, DEVICE_ID, path, FIREBASE_AUTH_SECRET);
  } else {
    snprintf(url, sizeof(url), "%s/devices/%s/%s.json", FIREBASE_BASE_URL,
             DEVICE_ID, path);
  }

  // Allocate response buffer if caller expects response data
  struct http_response_buffer res_buf = {0};
  if (response_data) {
    res_buf.capacity = 256;
    res_buf.data = malloc(res_buf.capacity);
    if (res_buf.data) {
      res_buf.data[0] = '\0';
    }
  }

  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .timeout_ms = 8000,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .event_handler = http_event_handler,
      .user_data = response_data ? &res_buf : NULL,
  };

  if (strcmp(method, "PATCH") == 0)
    config.method = HTTP_METHOD_PATCH;
  else if (strcmp(method, "PUT") == 0)
    config.method = HTTP_METHOD_PUT;
  else if (strcmp(method, "DELETE") == 0)
    config.method = HTTP_METHOD_DELETE;
  else if (strcmp(method, "POST") == 0)
    config.method = HTTP_METHOD_POST;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    if (res_buf.data)
      free(res_buf.data);
    xSemaphoreGive(s_http_mutex);
    return ESP_FAIL;
  }

  if (post_data) {
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");
  }

  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200 && status_code != 204) {
      ESP_LOGW(TAG,
               "\033[1;33m[HTTP Warning]\033[0m %s response status code: %d",
               method, status_code);
    }

    if (response_data && status_code == 200) {
      *response_data = res_buf.data;
    } else {
      if (res_buf.data)
        free(res_buf.data);
    }
  } else {
    ESP_LOGE(TAG, "\033[1;31m[HTTP Failed]\033[0m %s request failed: %s",
             method, esp_err_to_name(err));
    if (res_buf.data)
      free(res_buf.data);
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
  char *json_str = state_builder_create_json_string();
  if (!json_str) {
    ESP_LOGE(TAG, "Failed to build state JSON");
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = firebase_http_request("", "PATCH", json_str, NULL);
  free(json_str);
  return err;
}

esp_err_t firebase_update_ir_signal(const char *protocol,
                                    const char *ir_value_str) {
  size_t size = strlen(protocol) + strlen(ir_value_str) + 128;
  char *payload = malloc(size);
  if (!payload) {
    ESP_LOGE(TAG, "Failed to allocate memory for IR signal payload");
    return ESP_ERR_NO_MEM;
  }

  struct timeval tv;
  gettimeofday(&tv, NULL);
  snprintf(payload, size,
           "{\"ir_signal\": {\"protocol\": \"%s\", \"last_value\": \"%s\", "
           "\"timestamp\": %ld}}",
           protocol, ir_value_str, (long)tv.tv_sec);

  esp_err_t err = firebase_http_request("", "PATCH", payload, NULL);
  free(payload);
  return err;
}

void firebase_trigger_update(void) {
  if (s_firebase_event_group) {
    xEventGroupSetBits(s_firebase_event_group, FIREBASE_UPDATE_BIT);
  }
}

// ---------------------------------------------------------------------------
// Poll task
// ---------------------------------------------------------------------------

static void firebase_poll_task(void *pvParameters) {
  // Wait until WiFi has a valid IP before attempting any HTTP request.
  ESP_LOGI(TAG, "Firebase poll task waiting for WiFi...");
  xEventGroupWaitBits(g_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                      portMAX_DELAY);

  ESP_LOGI(TAG, "Firebase poll task started");
  firebase_update_status("online");

  // Fetch and send Matter Setup Payload
  char qr_buf[256];
  char manual_buf[32];
  matter_manager_get_setup_payload(qr_buf, sizeof(qr_buf), manual_buf,
                                   sizeof(manual_buf));
  if (strlen(qr_buf) > 0) {
    char payload[384];
    snprintf(
        payload, sizeof(payload),
        "{\"matter_payload\": {\"qr_code\": \"%s\", \"manual_code\": \"%s\"}}",
        qr_buf, manual_buf);
    firebase_http_request("", "PATCH", payload, NULL);
  }

  while (1) {
    // Check the deferred-update flag (thread-safe via event group).
    EventBits_t fb_bits = xEventGroupGetBits(s_firebase_event_group);
    if (fb_bits & FIREBASE_UPDATE_BIT) {
      xEventGroupClearBits(s_firebase_event_group, FIREBASE_UPDATE_BIT);
      firebase_update_full_state();
    }

    // Only poll when connected
    EventBits_t wifi_bits = xEventGroupGetBits(g_wifi_event_group);
    if (!(wifi_bits & WIFI_CONNECTED_BIT)) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    char *response = NULL;
    esp_err_t err = firebase_http_request("commands", "GET", NULL, &response);

    if (err == ESP_OK && response != NULL) {
      if (strcmp(response, "null") != 0) {
        cJSON *json = cJSON_Parse(response);
        if (json != NULL) {
          // Use the unified command dispatcher
          esp_err_t cmd_err = command_dispatcher_execute(json);

          if (cmd_err == ESP_OK) {
            // Command executed — clear it (state update is deferred via
            // EventGroup)
            firebase_http_request("commands", "DELETE", NULL, NULL);
          } else {
            ESP_LOGW(TAG, "Firebase command rejected: %s",
                     esp_err_to_name(cmd_err));
            // Still delete the malformed command to avoid infinite retry
            firebase_http_request("commands", "DELETE", NULL, NULL);
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
    ESP_LOGW(TAG, "Firebase not configured! Set FIREBASE_BASE_URL in "
                  "firebase_manager.h.");
    return ESP_ERR_INVALID_STATE;
  }

  // Create the event group for thread-safe deferred updates.
  s_firebase_event_group = xEventGroupCreate();
  if (!s_firebase_event_group) {
    ESP_LOGE(TAG, "Failed to create Firebase event group");
    return ESP_ERR_NO_MEM;
  }

  // Create the HTTP mutex before spawning the poll task.
  s_http_mutex = xSemaphoreCreateMutex();
  if (!s_http_mutex) {
    ESP_LOGE(TAG, "Failed to create HTTP mutex");
    return ESP_ERR_NO_MEM;
  }

  xTaskCreate(firebase_poll_task, "firebase_poll", 8192, NULL, 5, NULL);
  ESP_LOGI(TAG, "Firebase manager initialized");
  return ESP_OK;
}
