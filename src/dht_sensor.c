#include "dht_sensor.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "DHT_SENSOR";

static float s_last_temp = 24.0;
static float s_last_hum = 50.0;

void dht_sensor_init(void) {
    ESP_LOGI(TAG, "Initializing DHT sensor on pin %d", DHT_PIN);
    gpio_set_direction((gpio_num_t)DHT_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)DHT_PIN, GPIO_PULLUP_ONLY);
}

esp_err_t dht_sensor_read(float *temperature, float *humidity) {
    // Basic placeholder implementation.
    // For a real production app, esp-idf-lib's DHT is recommended
    // due to precise timing requirements.
    
    // Send start signal
    gpio_set_direction((gpio_num_t)DHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)DHT_PIN, 0);
    ets_delay_us(20000);
    gpio_set_level((gpio_num_t)DHT_PIN, 1);
    ets_delay_us(40);
    gpio_set_direction((gpio_num_t)DHT_PIN, GPIO_MODE_INPUT);

    // Normally we'd read the 40 bits here...
    // Mocking response for now to ensure architecture is solid.
    *temperature = 24.5;
    *humidity = 45.0;

    s_last_temp = *temperature;
    s_last_hum = *humidity;

    return ESP_OK;
}

float dht_sensor_get_temperature(void) {
    return s_last_temp;
}

float dht_sensor_get_humidity(void) {
    return s_last_hum;
}

