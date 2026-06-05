#ifndef PWM_MANAGER_H
#define PWM_MANAGER_H

#include <stdint.h>

#define PWM_LAMP_PIN 22
#define PWM_RGB_R_PIN 23
#define PWM_RGB_G_PIN 25
#define PWM_RGB_B_PIN 26

void pwm_manager_init(void);
void pwm_set_duty(uint8_t pin, uint32_t duty);

#endif // PWM_MANAGER_H
