#include "pwm_manager.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "PWM_MANAGER";

void pwm_manager_init(void) {
    ESP_LOGI(TAG, "Initializing PWM for Lamp and RGB");

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT, // 0-255
        .freq_hz          = 5000,             // 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    uint8_t pwm_pins[] = {PWM_LAMP_PIN, PWM_RGB_R_PIN, PWM_RGB_G_PIN, PWM_RGB_B_PIN};
    for(int i = 0; i < 4; i++) {
        ledc_channel_config_t ledc_channel = {
            .speed_mode     = LEDC_LOW_SPEED_MODE,
            .channel        = (ledc_channel_t)i,
            .timer_sel      = LEDC_TIMER_0,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = pwm_pins[i],
            .duty           = 0,
            .hpoint         = 0
        };
        ledc_channel_config(&ledc_channel);
    }
}

void pwm_set_duty(uint8_t pin, uint32_t duty) {
    ledc_channel_t channel = LEDC_CHANNEL_0;
    const char* pin_name = "PWM_UNKNOWN";
    if (pin == PWM_LAMP_PIN) { channel = LEDC_CHANNEL_0; pin_name = "LAMP"; }
    else if (pin == PWM_RGB_R_PIN) { channel = LEDC_CHANNEL_1; pin_name = "RGB_RED"; }
    else if (pin == PWM_RGB_G_PIN) { channel = LEDC_CHANNEL_2; pin_name = "RGB_GREEN"; }
    else if (pin == PWM_RGB_B_PIN) { channel = LEDC_CHANNEL_3; pin_name = "RGB_BLUE"; }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
    
    int pct = (duty * 100) / 255;
    ESP_LOGI(TAG, "PWM [%s] Pin %d ➔ Duty: \033[1;35m%d\033[0m (%d%%)", pin_name, pin, (int)duty, pct);
}

uint32_t pwm_get_duty(uint8_t pin) {
    ledc_channel_t channel = LEDC_CHANNEL_0;
    if (pin == PWM_LAMP_PIN) channel = LEDC_CHANNEL_0;
    else if (pin == PWM_RGB_R_PIN) channel = LEDC_CHANNEL_1;
    else if (pin == PWM_RGB_G_PIN) channel = LEDC_CHANNEL_2;
    else if (pin == PWM_RGB_B_PIN) channel = LEDC_CHANNEL_3;

    return ledc_get_duty(LEDC_LOW_SPEED_MODE, channel);
}

