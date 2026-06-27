#include "mock_status.h"
#include "gpio_manager.h"
#include "pwm_manager.h"
#include "nvs_manager.h"
#include "ir_manager.h"
#include "ota_manager.h"
#include "mqtt_manager.h"
#include "ac_timer_manager.h"
#include "wifi_manager.h"
// #include "dht_sensor.h"
#include "esp_system.h"
#include "esp_log.h"
#include <string.h>

// Global mock status instance
mock_status_t mock_status;

void mock_status_reset(void) {
    if (mock_status.ac_timer.last_ir_code) {
        cJSON_Delete(mock_status.ac_timer.last_ir_code);
    }
    memset(&mock_status, 0, sizeof(mock_status_t));
}

// GPIO Manager Mocks
void gpio_set_relay_state(uint8_t pin, int state) {
    mock_status.gpio.call_count++;
    mock_status.gpio.last_pin = pin;
    mock_status.gpio.last_state = state;
    if (pin < 32) {
        mock_status.gpio.return_val[pin] = state;
    }
}

int gpio_get_relay_state(uint8_t pin) {
    if (pin < 32) {
        return mock_status.gpio.return_val[pin];
    }
    return 0;
}

// PWM Manager Mocks
void pwm_set_duty(uint8_t pin, uint32_t duty) {
    mock_status.pwm.call_count++;
    mock_status.pwm.last_pin = pin;
    mock_status.pwm.last_duty = duty;
    if (pin < 32) {
        mock_status.pwm.return_val[pin] = duty;
    }
}

uint32_t pwm_get_duty(uint8_t pin) {
    if (pin < 32) {
        return mock_status.pwm.return_val[pin];
    }
    return 0;
}

// NVS Manager Mocks
int nvs_get_target_temp(int default_val) {
    mock_status.nvs.get_call_count++;
    if (mock_status.nvs.get_return_val != 0) {
        return mock_status.nvs.get_return_val;
    }
    return default_val;
}

void nvs_save_target_temp(int temp) {
    mock_status.nvs.save_call_count++;
    mock_status.nvs.last_saved_temp = temp;
}

// IR Manager Mocks
void ir_send_raw(uint16_t *durations, size_t length, uint32_t freq_hz) {
    mock_status.ir.send_raw_call_count++;
    mock_status.ir.last_count = length;
    mock_status.ir.last_frequency = freq_hz;
    size_t copy_count = length < 512 ? length : 512;
    memcpy(mock_status.ir.last_durations, durations, copy_count * sizeof(uint16_t));
}


void ir_manager_start_learning(void) {
    mock_status.ir.start_learning_call_count++;
}

// OTA Manager Mocks
void ota_manager_start(const char *url) {
    mock_status.ota.start_call_count++;
    if (url) {
        strncpy(mock_status.ota.last_url, url, sizeof(mock_status.ota.last_url) - 1);
    }
}

// MQTT Manager Mocks
void mqtt_manager_publish_event(const char *json_string) {
    mock_status.mqtt.publish_call_count++;
    if (json_string) {
        strncpy(mock_status.mqtt.last_published_event, json_string, sizeof(mock_status.mqtt.last_published_event) - 1);
    }
}

// AC Timer Manager Mocks
void ac_timer_set(int seconds, const cJSON *ir_code_json) {
    mock_status.ac_timer.set_call_count++;
    mock_status.ac_timer.last_seconds = seconds;
    if (mock_status.ac_timer.last_ir_code) {
        cJSON_Delete(mock_status.ac_timer.last_ir_code);
        mock_status.ac_timer.last_ir_code = NULL;
    }
    if (ir_code_json) {
        mock_status.ac_timer.last_ir_code = cJSON_Duplicate(ir_code_json, 1);
    }
}

void ac_timer_cancel(void) {
    mock_status.ac_timer.cancel_call_count++;
}

int ac_timer_get_remaining(void) {
    return mock_status.ac_timer.remaining_return_val;
}

// WiFi Manager Mocks
int wifi_manager_get_rssi(void) {
    return mock_status.wifi.get_rssi_return_val;
}

// DHT Sensor Mocks
// float dht_sensor_get_temperature(void) {
//     return mock_status.dht.temperature_return_val;
// }

// float dht_sensor_get_humidity(void) {
//     return mock_status.dht.humidity_return_val;
// }

// ESP System Mocks
uint32_t esp_get_free_heap_size(void) {
    return mock_status.system.free_heap_return_val;
}

// Firebase update trigger stub (referenced extern in source files)
void firebase_trigger_update(void) {
    mock_status.firebase.trigger_update_call_count++;
}
