#ifndef STATE_BUILDER_H
#define STATE_BUILDER_H

/**
 * @file state_builder.h
 * @brief Unified device state JSON builder.
 *
 * Produces the canonical state snapshot consumed by Firebase, MQTT, and
 * any future protocol.  Having a single builder guarantees that every
 * consumer always sees the same field names, types, and structure.
 */

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build a cJSON object describing the full device state.
 *
 * The returned object contains:
 *   temperature, humidity, target_temperature, wifi_rssi, heap_free,
 *   pins { relay_1..4, pwm_lamp, pwm_rgb_r/g/b }
 *
 * @return A heap-allocated cJSON* — caller must call cJSON_Delete().
 *         Returns NULL on allocation failure.
 */
cJSON *state_builder_create_full(void);

/**
 * @brief Build a compact JSON string from the full state.
 *
 * Convenience wrapper: calls state_builder_create_full(), serialises
 * it with cJSON_PrintUnformatted(), and cleans up.
 *
 * @return A heap-allocated char* — caller must call free().
 *         Returns NULL on allocation failure.
 */
char *state_builder_create_json_string(void);

#ifdef __cplusplus
}
#endif

#endif // STATE_BUILDER_H
