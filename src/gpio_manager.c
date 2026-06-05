#include "gpio_manager.h"
#include "driver/gpio.h"
#include "nvs_manager.h"
#include "esp_log.h"

static const char *TAG = "GPIO_MANAGER";

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
    gpio_set_level(RELAY_1_PIN, nvs_get_pin_state(RELAY_1_PIN, 0));
    gpio_set_level(RELAY_2_PIN, nvs_get_pin_state(RELAY_2_PIN, 0));
    gpio_set_level(RELAY_3_PIN, nvs_get_pin_state(RELAY_3_PIN, 0));
    gpio_set_level(RELAY_4_PIN, nvs_get_pin_state(RELAY_4_PIN, 0));
}

void gpio_set_relay_state(uint8_t pin, int state) {
    gpio_set_level(pin, state);
    nvs_save_pin_state(pin, state);
    ESP_LOGI(TAG, "Set Relay %d to %d", pin, state);
}

int gpio_get_relay_state(uint8_t pin) {
    return gpio_get_level(pin);
}
