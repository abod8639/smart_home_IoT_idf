#include "state_builder.h"
#include "device_config.h"
#include "dht_sensor.h"
#include "gpio_manager.h"
#include "pwm_manager.h"
#include "nvs_manager.h"
#include "wifi_manager.h"
#include "esp_system.h"
#include "cJSON.h"

cJSON *state_builder_create_full(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddNumberToObject(root, "temperature",       dht_sensor_get_temperature());
    cJSON_AddNumberToObject(root, "humidity",           dht_sensor_get_humidity());
    cJSON_AddNumberToObject(root, "target_temperature", nvs_get_target_temp(24));
    cJSON_AddNumberToObject(root, "wifi_rssi",          wifi_manager_get_rssi());
    cJSON_AddNumberToObject(root, "heap_free",          (double)esp_get_free_heap_size());

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

    return root;
}

char *state_builder_create_json_string(void) {
    cJSON *root = state_builder_create_full();
    if (!root) return NULL;

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;  // may be NULL if cJSON_PrintUnformatted fails
}
