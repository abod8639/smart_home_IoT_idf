#ifndef COMMAND_DISPATCHER_H
#define COMMAND_DISPATCHER_H

/**
 * @file command_dispatcher.h
 * @brief Unified command dispatcher — single entry point for all protocols.
 *
 * MQTT, Firebase, Matter, and any future protocol parse their incoming
 * message into a cJSON object and call command_dispatcher_execute().
 * This eliminates the duplicated switch/if-else chains that previously
 * existed in mqtt_manager.c and firebase_manager.c.
 */

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Execute a command described by a cJSON object.
 *
 * Supported actions:
 *   "set_relay"    — { pin: int, value: 0|1 }
 *   "set_pwm"      — { pin: int, value: 0-255 }
 *   "control_ac"   — { isOn: 0|1, target_temp: int }
 *   "ir_send"      — { protocol: "RAW", value: "csv", bits: int, frequency?: int }
 *   "ir_learn"     — (no extra fields)
 *   "ota_start"    — { url: string }
 *   "get_state"    — (triggers state publish; handled by caller)
 *
 * @param json   The parsed command object. Ownership stays with the caller.
 * @return ESP_OK if the command was recognised and executed,
 *         ESP_ERR_INVALID_ARG if validation failed,
 *         ESP_ERR_NOT_FOUND if the action is unknown.
 */
esp_err_t command_dispatcher_execute(const cJSON *json);

#ifdef __cplusplus
}
#endif

#endif // COMMAND_DISPATCHER_H
