#include "mqtt_manager.h"
#include "mqtt_credentials.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "gpio_manager.h"
#include "pwm_manager.h"
#include "nvs_manager.h"
#include "wifi_manager.h"
#include "dht_sensor.h"
#include "ir_manager.h"
#include "ota_manager.h"
#include <string.h>

static const char *TAG = "MQTT_MANAGER";
static esp_mqtt_client_handle_t client = NULL;

extern void firebase_trigger_update(void);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void mqtt_manager_publish_state(void) {
    if (!client) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event",            "state");
    cJSON_AddNumberToObject(root, "temperature",      dht_sensor_get_temperature());
    cJSON_AddNumberToObject(root, "humidity",         dht_sensor_get_humidity());
    cJSON_AddNumberToObject(root, "wifi_rssi",        wifi_manager_get_rssi());
    cJSON_AddNumberToObject(root, "heap_free",        esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "target_temperature", nvs_get_target_temp(24));

    cJSON *pins = cJSON_CreateObject();
    cJSON_AddNumberToObject(pins, "relay_1",   gpio_get_relay_state(RELAY_1_PIN));
    cJSON_AddNumberToObject(pins, "relay_2",   gpio_get_relay_state(RELAY_2_PIN));
    cJSON_AddNumberToObject(pins, "relay_3",   gpio_get_relay_state(RELAY_3_PIN));
    cJSON_AddNumberToObject(pins, "relay_4",   gpio_get_relay_state(RELAY_4_PIN));
    cJSON_AddNumberToObject(pins, "pwm_lamp",  pwm_get_duty(PWM_LAMP_PIN));
    cJSON_AddNumberToObject(pins, "pwm_rgb_r", pwm_get_duty(PWM_RGB_R_PIN));
    cJSON_AddNumberToObject(pins, "pwm_rgb_g", pwm_get_duty(PWM_RGB_G_PIN));
    cJSON_AddNumberToObject(pins, "pwm_rgb_b", pwm_get_duty(PWM_RGB_B_PIN));
    cJSON_AddItemToObject(root, "pins", pins);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_mqtt_client_publish(client, MQTT_TOPIC_STATE, json_str, 0, 1, 1); // QoS 1, Retain 1
    free(json_str);
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
// Command Handler
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
    if (action && action->valuestring) {
        if (strcmp(action->valuestring, "get_state") == 0) {
            ESP_LOGD(TAG, "Poll packet received");
        } else {
            ESP_LOGI(TAG, "\033[1;32m[MQTT]\033[0m Action: \033[1;36m%s\033[0m", action->valuestring);
        }

        if (strcmp(action->valuestring, "set_relay") == 0) {
            cJSON *pin = cJSON_GetObjectItem(json, "pin");
            cJSON *val = cJSON_GetObjectItem(json, "value");
            if (pin && val) {
                gpio_set_relay_state(pin->valueint, val->valueint);
                int endpoint = gpio_pin_to_endpoint(pin->valueint);
                char update_buf[96];
                snprintf(update_buf, sizeof(update_buf),
                         "{\"event\":\"relay_update\",\"endpoint\":%d,\"state\":%d}",
                         endpoint, val->valueint);
                mqtt_manager_publish_event(update_buf);
                firebase_trigger_update();
            }
        } else if (strcmp(action->valuestring, "set_pwm") == 0) {
            cJSON *pin = cJSON_GetObjectItem(json, "pin");
            cJSON *val = cJSON_GetObjectItem(json, "value");
            if (pin && val) {
                pwm_set_duty(pin->valueint, val->valueint);
                int endpoint = (pin->valueint == PWM_RGB_R_PIN ||
                                pin->valueint == PWM_RGB_G_PIN ||
                                pin->valueint == PWM_RGB_B_PIN) ? 6 : 5;
                char update_buf[96];
                snprintf(update_buf, sizeof(update_buf),
                         "{\"event\":\"pwm_update\",\"endpoint\":%d,\"level\":%d}",
                         endpoint, val->valueint);
                mqtt_manager_publish_event(update_buf);
                firebase_trigger_update();
            }
        } else if (strcmp(action->valuestring, "control_ac") == 0) {
            cJSON *is_on      = cJSON_GetObjectItem(json, "isOn");
            cJSON *target_temp = cJSON_GetObjectItem(json, "target_temp");
            int   tgt          = target_temp ? target_temp->valueint : nvs_get_target_temp(24);

            if (target_temp) nvs_save_target_temp(tgt);
            if (is_on) gpio_set_relay_state(RELAY_3_PIN, is_on->valueint);

            char update_buf[96];
            snprintf(update_buf, sizeof(update_buf),
                     "{\"event\":\"ac_update\",\"isOn\":%s,\"target_temp\":%d}",
                     (is_on && is_on->valueint) ? "true" : "false", tgt);
            mqtt_manager_publish_event(update_buf);
            firebase_trigger_update();
        } else if (strcmp(action->valuestring, "ir_send") == 0) {
            cJSON *protocol = cJSON_GetObjectItem(json, "protocol");
            cJSON *value    = cJSON_GetObjectItem(json, "value");
            cJSON *bits     = cJSON_GetObjectItem(json, "bits");
            cJSON *freq     = cJSON_GetObjectItem(json, "frequency");

            if (protocol && value && bits && strcmp(protocol->valuestring, "RAW") == 0) {
                int count     = bits->valueint;
                int frequency = freq ? freq->valueint : 38;
                uint16_t *durations = malloc(sizeof(uint16_t) * count);
                if (durations) {
                    char *val_str = strdup(value->valuestring);
                    char *token   = strtok(val_str, ",");
                    int   idx     = 0;
                    while (token && idx < count) {
                        durations[idx++] = (uint16_t)atoi(token);
                        token = strtok(NULL, ",");
                    }
                    ir_send_raw(durations, idx, frequency * 1000);
                    free(val_str);
                    free(durations);
                }
            }
        } else if (strcmp(action->valuestring, "ir_learn") == 0) {
            ir_manager_start_learning();
        } else if (strcmp(action->valuestring, "ota_start") == 0) {
            cJSON *url = cJSON_GetObjectItem(json, "url");
            if (url && url->valuestring) {
                ota_manager_start(url->valuestring);
            }
        } else if (strcmp(action->valuestring, "get_state") == 0) {
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
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            // Set status to online
            esp_mqtt_client_publish(client, MQTT_TOPIC_STATUS, "online", 0, 1, 1);
            // Subscribe to commands
            esp_mqtt_client_subscribe(client, MQTT_TOPIC_CMD, 1);
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
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
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
