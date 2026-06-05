#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <stdint.h>

void nvs_manager_init(void);
void nvs_save_pin_state(uint8_t pin, int state);
int nvs_get_pin_state(uint8_t pin, int default_val);
void nvs_save_target_temp(int temp);
int nvs_get_target_temp(int default_val);

#endif // NVS_MANAGER_H
