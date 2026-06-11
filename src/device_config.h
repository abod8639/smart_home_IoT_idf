#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

/**
 * @file device_config.h
 * @brief Centralised device configuration — single source of truth.
 *
 * Every module that needs the device identity, pin layout, or hardware
 * validation should include this header instead of defining its own
 * constants.
 */

#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Device Identity
// ---------------------------------------------------------------------------
#define DEVICE_ID           "esp32_smart_home_1"
#define DEVICE_NAME         "SmartHome-ESP32"
#define FIRMWARE_VERSION    "2.0.0"

// ---------------------------------------------------------------------------
// GPIO — Relay Pins
// ---------------------------------------------------------------------------
#define RELAY_1_PIN   2
#define RELAY_2_PIN  18
#define RELAY_3_PIN  19
#define RELAY_4_PIN  21

// ---------------------------------------------------------------------------
// GPIO — PWM Pins (LEDC)
// ---------------------------------------------------------------------------
#define PWM_LAMP_PIN   22
#define PWM_RGB_R_PIN  23
#define PWM_RGB_G_PIN  25
#define PWM_RGB_B_PIN  26

// ---------------------------------------------------------------------------
// GPIO — IR Pins (RMT)
// ---------------------------------------------------------------------------
#define IR_TX_PIN  33
#define IR_RX_PIN  32

// ---------------------------------------------------------------------------
// GPIO — Sensors
// ---------------------------------------------------------------------------
#define DHT_PIN  4

// ---------------------------------------------------------------------------
// Pin Validation Helpers
// Used by the command dispatcher to reject invalid pin numbers.
// ---------------------------------------------------------------------------

/** @brief Return true if `pin` is one of the four relay GPIOs. */
static inline bool is_valid_relay_pin(int pin) {
    return pin == RELAY_1_PIN  || pin == RELAY_2_PIN ||
           pin == RELAY_3_PIN  || pin == RELAY_4_PIN;
}

/** @brief Return true if `pin` is a PWM-capable GPIO (lamp or RGB). */
static inline bool is_valid_pwm_pin(int pin) {
    return pin == PWM_LAMP_PIN  || pin == PWM_RGB_R_PIN ||
           pin == PWM_RGB_G_PIN || pin == PWM_RGB_B_PIN;
}

// ---------------------------------------------------------------------------
// Endpoint Mapping — GPIO pin → Matter / Flutter endpoint ID
// ---------------------------------------------------------------------------
static inline int gpio_pin_to_endpoint(int pin) {
    if (pin == RELAY_1_PIN) return 1;
    if (pin == RELAY_2_PIN) return 2;
    if (pin == RELAY_3_PIN) return 3;
    if (pin == RELAY_4_PIN) return 4;
    return 0; // unknown
}

// ---------------------------------------------------------------------------
// Security Configuration
// ---------------------------------------------------------------------------
// Restricts OTA updates to URLs starting with this prefix. Leave empty ("")
// to allow any HTTPS URL during development/testing.
#define OTA_TRUSTED_URL_PREFIX ""

/** @brief Return the endpoint ID for a PWM pin (5 = lamp, 6 = RGB). */
static inline int pwm_pin_to_endpoint(int pin) {
    if (pin == PWM_LAMP_PIN) return 5;
    if (pin == PWM_RGB_R_PIN || pin == PWM_RGB_G_PIN || pin == PWM_RGB_B_PIN) return 6;
    return 0;
}

#endif // DEVICE_CONFIG_H
