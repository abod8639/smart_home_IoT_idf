#include "mqtt_manager.h"
#include "mqtt_credentials.h"
#include "device_config.h"
#include "state_builder.h"
#include "command_dispatcher.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "MQTT_MANAGER";
static esp_mqtt_client_handle_t client = NULL;

// ---------------------------------------------------------------------------
// Public Publish Helpers
// ---------------------------------------------------------------------------

void mqtt_manager_publish_state(void) {
    if (!client) return;

    // Use the unified state builder — adds "event":"state" on top.
    cJSON *root = state_builder_create_full();
    if (!root) return;

    cJSON_AddStringToObject(root, "event", "state");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str) {
        esp_mqtt_client_publish(client, MQTT_TOPIC_STATE, json_str, 0, 1, 1); // QoS 1, Retain 1
        free(json_str);
    }
}

void mqtt_manager_publish_event(const char *json_str) {
    if (!client || !json_str) return;
    esp_mqtt_client_publish(client, MQTT_TOPIC_EVENT, json_str, 0, 1, 0); // QoS 1, No Retain
}

void mqtt_manager_publish_sensor(const char *json_str) {
    if (!client || !json_str) return;
    esp_mqtt_client_publish(client, MQTT_TOPIC_SENSOR, json_str, 0, 1, 0); // QoS 1, No Retain
}

// ---------------------------------------------------------------------------
// Command Handler — delegates to the unified dispatcher
// ---------------------------------------------------------------------------

static void handle_mqtt_command(const char *data, int data_len) {
    char *buf = calloc(1, data_len + 1);
    if (!buf) return;
    memcpy(buf, data, data_len);

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        ESP_LOGW(TAG, "Unparseable packet: %s", buf);
        free(buf);
        return;
    }

    cJSON *action = cJSON_GetObjectItem(json, "action");
    if (action && cJSON_IsString(action)) {
        // Suppress noisy logs for polling heartbeats
        if (strcmp(action->valuestring, "get_state") != 0) {
            ESP_LOGI(TAG, "\033[1;32m[MQTT]\033[0m Action: \033[1;36m%s\033[0m", action->valuestring);
        }

        // Delegate to the unified command dispatcher
        esp_err_t err = command_dispatcher_execute(json);

        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Command '%s' failed: %s", action->valuestring, esp_err_to_name(err));
        }

        // Always publish state after any command (including get_state)
        if (strcmp(action->valuestring, "get_state") == 0 || err == ESP_OK) {
            mqtt_manager_publish_state();
        }
    } else {
        ESP_LOGI(TAG, "Got packet: %s", buf);
    }

    cJSON_Delete(json);
    free(buf);
}

// ---------------------------------------------------------------------------
// MQTT Event Handler
// ---------------------------------------------------------------------------

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t evt_client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            // Set status to online
            esp_mqtt_client_publish(evt_client, MQTT_TOPIC_STATUS, "online", 0, 1, 1);
            // Subscribe to commands
            esp_mqtt_client_subscribe(evt_client, MQTT_TOPIC_CMD, 1);
            // Publish initial state
            mqtt_manager_publish_state();
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGD(TAG, "MQTT_EVENT_DATA");
            if (strncmp(event->topic, MQTT_TOPIC_CMD, event->topic_len) == 0) {
                handle_mqtt_command(event->data, event->data_len);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGD(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

esp_err_t mqtt_manager_init(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USERNAME,
        .credentials.client_id = DEVICE_ID,
        .credentials.authentication.password = MQTT_PASSWORD,
        .session.last_will.topic = MQTT_TOPIC_STATUS,
        .session.last_will.msg = "offline",
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT client started");
    } else {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
    }
    return err;
}
