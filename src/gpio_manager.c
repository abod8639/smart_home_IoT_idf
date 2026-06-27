#include "gpio_manager.h"
#include "driver/gpio.h"
#include "nvs_manager.h"
#include "esp_log.h"

static const char *TAG = "GPIO_MANAGER";

static int s_relay_states[40] = {0};

static const uint8_t s_relay_pins[] = {
    RELAY_1_PIN, RELAY_2_PIN, RELAY_3_PIN, RELAY_4_PIN,
    RELAY_5_PIN, RELAY_6_PIN, RELAY_7_PIN, RELAY_8_PIN,
    RELAY_9_PIN, RELAY_10_PIN, RELAY_11_PIN, RELAY_12_PIN
};
#define NUM_RELAY_PINS (sizeof(s_relay_pins)/sizeof(s_relay_pins[0]))

void gpio_manager_init(void) {
    ESP_LOGI(TAG, "Initializing GPIOs for Relays");
    
    uint64_t mask = 0;
    for (int i = 0; i < NUM_RELAY_PINS; i++) {
        mask |= (1ULL << s_relay_pins[i]);
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Restore previous states from NVS
    for (int i = 0; i < NUM_RELAY_PINS; i++) {
        uint8_t pin = s_relay_pins[i];
        s_relay_states[pin] = nvs_get_pin_state(pin, 0);
        gpio_set_level(pin, s_relay_states[pin]);
    }
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
