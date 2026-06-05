#ifndef FIREBASE_MANAGER_H
#define FIREBASE_MANAGER_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Replace these with your actual Firebase Project URL and Secret
// e.g., "https://my-project-default-rtdb.firebaseio.com"
#define FIREBASE_BASE_URL "https://smart-home-69271-default-rtdb.firebaseio.com"

// If you have a Database Secret, put it here. Otherwise leave it empty if rules are open.
#define FIREBASE_AUTH_SECRET ""

#define FIREBASE_DEVICE_ID "esp32_smart_home_1"

/**
 * @brief Initialize the Firebase manager.
 *        This will start a FreeRTOS task that connects to Firebase to listen/poll for commands.
 */
esp_err_t firebase_manager_init(void);

/**
 * @brief Update the last received IR signal to Firebase
 * @param protocol The protocol name (e.g., "NEC")
 * @param ir_value_str The IR value as string (e.g., "0xFFE01F" or raw timings)
 */
esp_err_t firebase_update_ir_signal(const char* protocol, const char* ir_value_str);

/**
 * @brief Update the device status on Firebase
 * @param status The status string (e.g., "online", "offline")
 */
esp_err_t firebase_update_status(const char* status);

/**
 * @brief Update the full device state to Firebase
 */
esp_err_t firebase_update_full_state(void);

#ifdef __cplusplus
}
#endif

#endif // FIREBASE_MANAGER_H
