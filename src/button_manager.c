#include "button_manager.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_manager.h" // To trigger Captive Portal
#include "gpio_manager.h"

#define BOOT_BUTTON_PIN 0 // Default BOOT button on most ESP32 boards
#define LONG_PRESS_TIME_MS 3000

static const char *TAG = "BUTTON_MANAGER";

static void button_task(void *arg) {
  uint32_t press_duration = 0;
  bool portal_triggered = false;

  while (1) {
    // BOOT button is active low (pulled up externally or internally)
    if (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
      press_duration += 100;
      if (press_duration >= LONG_PRESS_TIME_MS && !portal_triggered) {
        // Turn on the onboard LED (GPIO 2) to indicate setup mode
        gpio_set_relay_state(RELAY_1_PIN, 1);
        ESP_LOGI(TAG, "\033[1;33m[BUTTON]\033[0m Long press detected. Starting "
                      "Captive Portal...");
        portal_triggered = true;

        // Trigger captive portal
        wifi_manager_start_captive_portal();
      }
    } else {
      press_duration = 0;
      portal_triggered = false;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Poll every 100ms
  }
}

void button_manager_init(void) {
  ESP_LOGI(TAG, "Initializing BOOT button manager");

  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&io_conf);

  xTaskCreate(button_task, "button_task", 3072, NULL, 5, NULL);
}
