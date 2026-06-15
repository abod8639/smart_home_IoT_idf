#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
void nvs_manager_init(void);
void nvs_save_pin_state(uint8_t pin, int state);
int nvs_get_pin_state(uint8_t pin, int default_val);
void nvs_save_target_temp(int temp);
int nvs_get_target_temp(int default_val);

void nvs_save_wifi_credentials(const char* ssid, const char* password);
bool nvs_get_wifi_credentials(char* ssid, size_t ssid_len, char* password, size_t password_len);

#endif // NVS_MANAGER_H
