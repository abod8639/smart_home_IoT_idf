#include "pwm_manager.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "PWM_MANAGER";

typedef struct {
    uint8_t pin;
    ledc_mode_t speed_mode;
    ledc_channel_t channel;
    const char* name;
} pwm_pin_map_t;

static const pwm_pin_map_t s_pwm_pins[] = {
    { PWM_LAMP_PIN,   LEDC_LOW_SPEED_MODE,  LEDC_CHANNEL_0, "LAMP" },
    { PWM_RGB_R_PIN,  LEDC_LOW_SPEED_MODE,  LEDC_CHANNEL_1, "RGB_RED" },
    { PWM_RGB_G_PIN,  LEDC_LOW_SPEED_MODE,  LEDC_CHANNEL_2, "RGB_GREEN" },
    { PWM_RGB_B_PIN,  LEDC_LOW_SPEED_MODE,  LEDC_CHANNEL_3, "RGB_BLUE" },
    { PWM_5_PIN,      LEDC_LOW_SPEED_MODE,  LEDC_CHANNEL_4, "GPIO_5" },
    { PWM_6_PIN,      LEDC_LOW_SPEED_MODE,  LEDC_CHANNEL_5, "GPIO_12" },
    { PWM_7_PIN,      LEDC_LOW_SPEED_MODE,  LEDC_CHANNEL_6, "GPIO_13" },
    { PWM_8_PIN,      LEDC_LOW_SPEED_MODE,  LEDC_CHANNEL_7, "GPIO_14" },
    { PWM_9_PIN,      LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, "GPIO_15" },
    { PWM_10_PIN,     LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, "GPIO_16" },
    { PWM_11_PIN,     LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, "GPIO_17" },
    { PWM_12_PIN,     LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_3, "GPIO_27" }
};
#define NUM_PWM_PINS (sizeof(s_pwm_pins)/sizeof(s_pwm_pins[0]))

void pwm_manager_init(void) {
    ESP_LOGI(TAG, "Initializing PWM for all configured pins");

    // Configure Low Speed LEDC Timer
    ledc_timer_config_t ledc_timer_low = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT, // 0-255
        .freq_hz          = 5000,             // 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer_low);

    // Configure High Speed LEDC Timer
    ledc_timer_config_t ledc_timer_high = {
        .speed_mode       = LEDC_HIGH_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT, // 0-255
        .freq_hz          = 5000,             // 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer_high);

    // Configure LEDC Channels
    for (int i = 0; i < NUM_PWM_PINS; i++) {
        ledc_channel_config_t ledc_channel = {
            .speed_mode     = s_pwm_pins[i].speed_mode,
            .channel        = s_pwm_pins[i].channel,
            .timer_sel      = LEDC_TIMER_0,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = s_pwm_pins[i].pin,
            .duty           = 0,
            .hpoint         = 0
        };
        ledc_channel_config(&ledc_channel);
    }
}

void pwm_set_duty(uint8_t pin, uint32_t duty) {
    for (int i = 0; i < NUM_PWM_PINS; i++) {
        if (s_pwm_pins[i].pin == pin) {
            ledc_set_duty(s_pwm_pins[i].speed_mode, s_pwm_pins[i].channel, duty);
            ledc_update_duty(s_pwm_pins[i].speed_mode, s_pwm_pins[i].channel);
            int pct = (duty * 100) / 255;
            ESP_LOGI(TAG, "PWM [%s] Pin %d ➔ Duty: \033[1;35m%d\033[0m (%d%%)", s_pwm_pins[i].name, pin, (int)duty, pct);
            return;
        }
    }
    ESP_LOGW(TAG, "pwm_set_duty: Pin %d not configured for PWM", pin);
}

uint32_t pwm_get_duty(uint8_t pin) {
    for (int i = 0; i < NUM_PWM_PINS; i++) {
        if (s_pwm_pins[i].pin == pin) {
            return ledc_get_duty(s_pwm_pins[i].speed_mode, s_pwm_pins[i].channel);
        }
    }
    return 0;
}

