#ifndef GPIO_MANAGER_H
#define GPIO_MANAGER_H

#include <stdint.h>
#include "esp_err.h"

// Define Relay Pins
#define RELAY_1_PIN 2
#define RELAY_2_PIN 18
#define RELAY_3_PIN 19
#define RELAY_4_PIN 21

// Initialize GPIOs
void gpio_manager_init(void);

// Set Relay state
void gpio_set_relay_state(uint8_t pin, int state);

// Get Relay state
int gpio_get_relay_state(uint8_t pin);

#endif // GPIO_MANAGER_H
