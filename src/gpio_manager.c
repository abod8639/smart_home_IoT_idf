#include "gpio_manager.h"
#include "driver/gpio.h"
#include "nvs_manager.h"
#include "esp_log.h"

static const char *TAG = "GPIO_MANAGER";

static int s_relay_states[40] = {0};

void gpio_manager_init(void) {
    ESP_LOGI(TAG, "Initializing GPIOs for Relays");
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL<<RELAY_1_PIN) | (1ULL<<RELAY_2_PIN) | (1ULL<<RELAY_3_PIN) | (1ULL<<RELAY_4_PIN),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Restore previous states from NVS
    s_relay_states[RELAY_1_PIN] = nvs_get_pin_state(RELAY_1_PIN, 0);
    s_relay_states[RELAY_2_PIN] = nvs_get_pin_state(RELAY_2_PIN, 0);
    s_relay_states[RELAY_3_PIN] = nvs_get_pin_state(RELAY_3_PIN, 0);
    s_relay_states[RELAY_4_PIN] = nvs_get_pin_state(RELAY_4_PIN, 0);

    gpio_set_level(RELAY_1_PIN, s_relay_states[RELAY_1_PIN]);
    gpio_set_level(RELAY_2_PIN, s_relay_states[RELAY_2_PIN]);
    gpio_set_level(RELAY_3_PIN, s_relay_states[RELAY_3_PIN]);
    gpio_set_level(RELAY_4_PIN, s_relay_states[RELAY_4_PIN]);
}

void gpio_set_relay_state(uint8_t pin, int state) {
    gpio_set_level(pin, state);
    if (pin < 40) {
        s_relay_states[pin] = state;
    }
    nvs_save_pin_state(pin, state);
    ESP_LOGI(TAG, "Relay Pin %d ➔ %s", pin, state ? "\033[1;32m[ ON ]\033[0m" : "\033[1;31m[ OFF ]\033[0m");
}

int gpio_get_relay_state(uint8_t pin) {
    if (pin < 40) {
        return s_relay_states[pin];
    }
    return gpio_get_level(pin);
}
