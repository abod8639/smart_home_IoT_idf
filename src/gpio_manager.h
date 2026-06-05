#ifndef GPIO_MANAGER_H
#define GPIO_MANAGER_H

#include <stdint.h>
#include "esp_err.h"

// ---------------------------------------------------------------------------
// Pin definitions
// ---------------------------------------------------------------------------
#define RELAY_1_PIN  2
#define RELAY_2_PIN  18
#define RELAY_3_PIN  19
#define RELAY_4_PIN  21

// ---------------------------------------------------------------------------
// Shared helper: map a relay GPIO pin to its Flutter/Matter endpoint ID.
// Centralised here to avoid duplicating the mapping in ws_server.c and
// firebase_manager.c.
// ---------------------------------------------------------------------------
static inline int gpio_pin_to_endpoint(int pin) {
    if (pin == RELAY_2_PIN) return 2;
    if (pin == RELAY_3_PIN) return 3;
    if (pin == RELAY_4_PIN) return 4;
    return 1; // RELAY_1_PIN or unknown → endpoint 1
}

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
