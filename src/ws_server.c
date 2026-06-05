#include "ws_server.h"
#include "cJSON.h"
#include "gpio_manager.h"
#include "pwm_manager.h"
#include "nvs_manager.h"
#include "wifi_manager.h"
#include "dht_sensor.h"
#include "ir_manager.h"
#include "ota_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WS_SERVER";
static httpd_handle_t s_server = NULL;

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
    char *msg;
};

static void ws_async_send(void *arg) {
    struct async_resp_arg *resp_arg = arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    char *msg = resp_arg->msg;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)msg;
    ws_pkt.len = strlen(msg);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(msg);
    free(resp_arg);
}

static esp_err_t send_hello(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "connected");
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)json_str;
    ws_pkt.len = strlen(json_str);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t err = httpd_ws_send_frame(req, &ws_pkt);
    free(json_str);
    return err;
}

static void send_state(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "state");
    cJSON_AddNumberToObject(root, "temperature", dht_sensor_get_temperature());
    cJSON_AddNumberToObject(root, "humidity", dht_sensor_get_humidity());

    cJSON *pins = cJSON_CreateObject();
    cJSON_AddNumberToObject(pins, "relay_1", gpio_get_relay_state(RELAY_1_PIN));
    cJSON_AddNumberToObject(pins, "relay_2", gpio_get_relay_state(RELAY_2_PIN));
    cJSON_AddNumberToObject(pins, "relay_3", gpio_get_relay_state(RELAY_3_PIN));
    cJSON_AddNumberToObject(pins, "relay_4", gpio_get_relay_state(RELAY_4_PIN));
    cJSON_AddNumberToObject(pins, "pwm_lamp", pwm_get_duty(PWM_LAMP_PIN));
    cJSON_AddNumberToObject(pins, "pwm_rgb_r", pwm_get_duty(PWM_RGB_R_PIN));
    cJSON_AddNumberToObject(pins, "pwm_rgb_g", pwm_get_duty(PWM_RGB_G_PIN));
    cJSON_AddNumberToObject(pins, "pwm_rgb_b", pwm_get_duty(PWM_RGB_B_PIN));
    cJSON_AddItemToObject(root, "pins", pins);

    cJSON_AddNumberToObject(root, "wifi_rssi", wifi_manager_get_rssi());
    cJSON_AddNumberToObject(root, "heap_free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "target_temperature", nvs_get_target_temp(24));

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)json_str;
    ws_pkt.len = strlen(json_str);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame(req, &ws_pkt);
    free(json_str);
}

static esp_err_t handle_ws_req(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return send_hello(req);
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) return ESP_ERR_NO_MEM;
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) { free(buf); return ret; }

        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
            ESP_LOGI(TAG, "Got packet: %s", ws_pkt.payload);
            cJSON *json = cJSON_Parse((char*)ws_pkt.payload);
            if (json) {
                cJSON *action = cJSON_GetObjectItem(json, "action");
                if (action && action->valuestring) {
                    if (strcmp(action->valuestring, "set_relay") == 0) {
                        cJSON *pin = cJSON_GetObjectItem(json, "pin");
                        cJSON *val = cJSON_GetObjectItem(json, "value");
                        if (pin && val) {
                            gpio_set_relay_state(pin->valueint, val->valueint);
                            
                            // Map Pin to Endpoint for Flutter compatibility
                            int endpoint = 1;
                            if (pin->valueint == RELAY_2_PIN) endpoint = 2;
                            else if (pin->valueint == RELAY_3_PIN) endpoint = 3;
                            else if (pin->valueint == RELAY_4_PIN) endpoint = 4;
                            
                            char update_buf[128];
                            snprintf(update_buf, sizeof(update_buf), 
                                     "{\"event\": \"relay_update\", \"endpoint\": %d, \"state\": %d}", 
                                     endpoint, val->valueint);
                            ws_server_broadcast(update_buf);
                        }
                    } else if (strcmp(action->valuestring, "set_pwm") == 0) {
                        cJSON *pin = cJSON_GetObjectItem(json, "pin");
                        cJSON *val = cJSON_GetObjectItem(json, "value");
                        if (pin && val) {
                            pwm_set_duty(pin->valueint, val->valueint);
                            
                            int endpoint = 5; // pwm_lamp
                            if (pin->valueint == PWM_RGB_R_PIN || pin->valueint == PWM_RGB_G_PIN || pin->valueint == PWM_RGB_B_PIN) {
                                endpoint = 6; // RGB
                            }
                            
                            char update_buf[128];
                            snprintf(update_buf, sizeof(update_buf), 
                                     "{\"event\": \"pwm_update\", \"endpoint\": %d, \"level\": %d}", 
                                     endpoint, val->valueint);
                            ws_server_broadcast(update_buf);
                        }
                    } else if (strcmp(action->valuestring, "control_ac") == 0) {
                        cJSON *is_on = cJSON_GetObjectItem(json, "isOn");
                        cJSON *target_temp = cJSON_GetObjectItem(json, "target_temp");
                        
                        if (target_temp) {
                            nvs_save_target_temp(target_temp->valueint);
                        }
                        if (is_on) {
                            gpio_set_relay_state(RELAY_3_PIN, is_on->valueint); // Relay 3 controls AC power
                        }
                        
                        char update_buf[128];
                        snprintf(update_buf, sizeof(update_buf), 
                                 "{\"event\": \"ac_update\", \"isOn\": %s, \"target_temp\": %d}", 
                                 (is_on && is_on->valueint) ? "true" : "false", 
                                 target_temp ? target_temp->valueint : nvs_get_target_temp(24));
                        ws_server_broadcast(update_buf);
                        
                    } else if (strcmp(action->valuestring, "ir_send") == 0) {
                        cJSON *protocol = cJSON_GetObjectItem(json, "protocol");
                        cJSON *value = cJSON_GetObjectItem(json, "value");
                        cJSON *bits = cJSON_GetObjectItem(json, "bits");
                        cJSON *freq = cJSON_GetObjectItem(json, "frequency");
                        
                        if (protocol && value && bits && strcmp(protocol->valuestring, "RAW") == 0) {
                            int count = bits->valueint;
                            int frequency = freq ? freq->valueint : 38;
                            uint16_t *durations = malloc(sizeof(uint16_t) * count);
                            if (durations) {
                                char *val_str = strdup(value->valuestring);
                                char *token = strtok(val_str, ",");
                                int idx = 0;
                                while (token && idx < count) {
                                    durations[idx++] = atoi(token);
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
                        send_state(req);
                    }
                }
                cJSON_Delete(json);
            }
        }
        free(buf);
    }
    return ret;
}

httpd_handle_t ws_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_uri_t ws = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = handle_ws_req,
            .user_ctx   = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(s_server, &ws);
        ESP_LOGI(TAG, "WebSocket server started on /ws");
    }
    return s_server;
}

void ws_server_broadcast(const char *msg) {
    if (!s_server) return;

    size_t fds = 10;
    int client_fds[10];
    if (httpd_get_client_list(s_server, &fds, client_fds) == ESP_OK) {
        for (size_t i = 0; i < fds; i++) {
            struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
            if (resp_arg) {
                resp_arg->hd = s_server;
                resp_arg->fd = client_fds[i];
                resp_arg->msg = strdup(msg);
                httpd_queue_work(s_server, ws_async_send, resp_arg);
            }
        }
    }
}
