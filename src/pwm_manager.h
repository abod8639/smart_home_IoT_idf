#ifndef PWM_MANAGER_H
#define PWM_MANAGER_H

#include <stdint.h>
#include "device_config.h"   // Pin definitions (PWM_LAMP_PIN, PWM_RGB_*)

void pwm_manager_init(void);
void pwm_set_duty(uint8_t pin, uint32_t duty);
uint32_t pwm_get_duty(uint8_t pin);

#endif // PWM_MANAGER_H
