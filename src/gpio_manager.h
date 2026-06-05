#ifndef GPIO_MANAGER_H
#define GPIO_MANAGER_H

#include <stdint.h>
#include "esp_err.h"
#include "device_config.h"   // Pin definitions and gpio_pin_to_endpoint()

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/** Initialise all relay GPIOs and restore saved states from NVS. */
void gpio_manager_init(void);

/** Set a relay GPIO to state (1 = ON, 0 = OFF) and persist to NVS. */
void gpio_set_relay_state(uint8_t pin, int state);

/** Return the current logical level of a relay GPIO. */
int gpio_get_relay_state(uint8_t pin);

#endif // GPIO_MANAGER_H
