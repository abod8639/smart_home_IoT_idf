#ifndef DHT_SENSOR_H
#define DHT_SENSOR_H

#include "esp_err.h"

#define DHT_PIN 4

// Initialize DHT
void dht_sensor_init(void);

// Read DHT22
esp_err_t dht_sensor_read(float *temperature, float *humidity);

float dht_sensor_get_temperature(void);
float dht_sensor_get_humidity(void);

#endif // DHT_SENSOR_H
