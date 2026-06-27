#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void nvs_manager_init(void);
void nvs_save_pin_state(uint8_t pin, int state);
int  nvs_get_pin_state(uint8_t pin, int default_val);
void nvs_save_target_temp(int temp);
int  nvs_get_target_temp(int default_val);

void nvs_save_wifi_credentials(const char* ssid, const char* password);
bool nvs_get_wifi_credentials(char* ssid, size_t ssid_len, char* password, size_t password_len);

// ---------------------------------------------------------------------------
// Matter Endpoint Persistence
// Stores up to NVS_MAX_MATTER_DEVICES endpoints across reboots.
// Each slot holds: device_type (int32) and pin_num (int32).
// ---------------------------------------------------------------------------
#define NVS_MAX_MATTER_DEVICES 8

/**
 * @brief Persist a Matter endpoint mapping to NVS.
 * @param slot        Slot index 0 .. NVS_MAX_MATTER_DEVICES-1
 * @param device_type 1 = on_off_light, 2 = on_off_plugin_unit, 3 = dimmable_light
 * @param pin_num     GPIO pin number
 */
void nvs_save_matter_device(int slot, int device_type, int pin_num);

/**
 * @brief Read a persisted Matter endpoint from NVS.
 * @param slot        Slot index to read
 * @param device_type Output — device type (unchanged if slot is empty)
 * @param pin_num     Output — GPIO pin (unchanged if slot is empty)
 * @return true if the slot contained valid data, false if empty / not found
 */
bool nvs_get_matter_device(int slot, int *device_type, int *pin_num);

/**
 * @brief Return the number of persisted Matter devices (0..NVS_MAX_MATTER_DEVICES).
 */
int nvs_get_matter_device_count(void);

/**
 * @brief Increment the persisted Matter device count by one.
 *        Called after a successful nvs_save_matter_device().
 */
void nvs_increment_matter_device_count(void);

/**
 * @brief Erase all persisted Matter endpoints (e.g., on factory reset).
 */
void nvs_clear_matter_devices(void);

#endif // NVS_MANAGER_H
