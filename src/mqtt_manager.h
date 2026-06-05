#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdint.h>
#include "esp_err.h"
#include "device_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// MQTT Topic Definitions (derived from the centralised DEVICE_ID)
// ---------------------------------------------------------------------------
#define MQTT_TOPIC_CMD    "smarthome/" DEVICE_ID "/cmd"
#define MQTT_TOPIC_STATE  "smarthome/" DEVICE_ID "/state"
#define MQTT_TOPIC_SENSOR "smarthome/" DEVICE_ID "/sensor"
#define MQTT_TOPIC_EVENT  "smarthome/" DEVICE_ID "/event"
#define MQTT_TOPIC_STATUS "smarthome/" DEVICE_ID "/status"

/**
 * @brief Initialize the MQTT client, register callbacks, and connect to the broker.
 */
esp_err_t mqtt_manager_init(void);

/**
 * @brief Publish a generic event (like relay update, pwm update).
 * @param json_str The JSON string to publish.
 */
void mqtt_manager_publish_event(const char *json_str);

/**
 * @brief Publish sensor data (temperature, humidity).
 * @param json_str The JSON string to publish.
 */
void mqtt_manager_publish_sensor(const char *json_str);

/**
 * @brief Publish the full state of the device.
 */
void mqtt_manager_publish_state(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_MANAGER_H
