// #include "dht_sensor.h"
// #include "device_config.h"
// #include "driver/gpio.h"
// #include "rom/ets_sys.h"
// #include "esp_log.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "freertos/semphr.h"

// static const char *TAG = "DHT_SENSOR";

// static float s_last_temp = 0.0f;
// static float s_last_hum  = 0.0f;

// // Mutex protecting s_last_temp and s_last_hum against concurrent
// // read/write from dht_monitor_task and state_builder / MQTT tasks.
// static SemaphoreHandle_t s_dht_mutex = NULL;

// // Spinlock to guard timing-critical bit-reading against FreeRTOS preemption
// static portMUX_TYPE s_dht_spinlock = portMUX_INITIALIZER_UNLOCKED;

// void dht_sensor_init(void) {
//     ESP_LOGI(TAG, "Initializing DHT22 sensor on GPIO %d", DHT_PIN);
//     gpio_set_pull_mode((gpio_num_t)DHT_PIN, GPIO_PULLUP_ONLY);
//     s_dht_mutex = xSemaphoreCreateMutex();
// }

// //
// ---------------------------------------------------------------------------
// // DHT22 bit-level protocol implementation
// // Timing reference: DHT22 datasheet (Aosong Electronics)
// //
// ---------------------------------------------------------------------------

// /**
//  * @brief Wait for a GPIO level change within a timeout (in microseconds).
//  *        Busy-waits 1us at a time — used only during the ~3ms data burst.
//  * @return Remaining timeout (≥0), or -1 on timeout.
//  */
// static int dht_await_level(int expected_level, int timeout_us) {
//     while (gpio_get_level((gpio_num_t)DHT_PIN) != expected_level) {
//         if (--timeout_us <= 0) return -1;
//         ets_delay_us(1);
//     }
//     return timeout_us;
// }

// esp_err_t dht_sensor_read(float *temperature, float *humidity) {
//     uint8_t data[5] = {0};
//     esp_err_t err = ESP_OK;

//     // ------------------------------------------------------------------
//     // 1. Start signal: pull LOW ≥1 ms, then release HIGH 20-40 µs
//     // ------------------------------------------------------------------
//     gpio_set_direction((gpio_num_t)DHT_PIN, GPIO_MODE_OUTPUT_OD);
//     gpio_set_level((gpio_num_t)DHT_PIN, 0);
//     vTaskDelay(pdMS_TO_TICKS(20));           // 20 ms LOW — well within spec
//     gpio_set_level((gpio_num_t)DHT_PIN, 1);
//     ets_delay_us(30);                        // 20-40 µs HIGH

//     // Switch to input to read DHT response under critical section
//     portENTER_CRITICAL(&s_dht_spinlock);
//     gpio_set_direction((gpio_num_t)DHT_PIN, GPIO_MODE_INPUT);

//     // ------------------------------------------------------------------
//     // 2. DHT response: ~80 µs LOW → ~80 µs HIGH
//     // ------------------------------------------------------------------
//     if (dht_await_level(0, 100) < 0) {
//         portEXIT_CRITICAL(&s_dht_spinlock);
//         ESP_LOGW(TAG, "DHT22 start LOW timeout");
//         return ESP_ERR_TIMEOUT;
//     }
//     if (dht_await_level(1, 100) < 0) {
//         portEXIT_CRITICAL(&s_dht_spinlock);
//         ESP_LOGW(TAG, "DHT22 start HIGH timeout");
//         return ESP_ERR_TIMEOUT;
//     }
//     if (dht_await_level(0, 100) < 0) {
//         portEXIT_CRITICAL(&s_dht_spinlock);
//         ESP_LOGW(TAG, "DHT22 response timeout");
//         return ESP_ERR_TIMEOUT;
//     }

//     // ------------------------------------------------------------------
//     // 3. Read 40 bits (5 bytes):
//     //    Each bit starts with ~50 µs LOW, then:
//     //      "0" = 26-28 µs HIGH   "1" = 70 µs HIGH
//     //    Sample at 40 µs after the HIGH edge.
//     // ------------------------------------------------------------------
//     for (int i = 0; i < 40; i++) {
//         // Wait for bit HIGH edge (end of 50 µs LOW)
//         if (dht_await_level(1, 65) < 0) {
//             err = ESP_ERR_TIMEOUT;
//             break;
//         }
//         // Sample after 40 µs — '0' is gone (<28µs), '1' is still HIGH (70µs)
//         ets_delay_us(40);
//         data[i / 8] <<= 1;
//         if (gpio_get_level((gpio_num_t)DHT_PIN)) {
//             data[i / 8] |= 1;
//         }
//         // Wait for HIGH to end before next bit
//         if (dht_await_level(0, 80) < 0) {
//             err = ESP_ERR_TIMEOUT;
//             break;
//         }
//     }
//     portEXIT_CRITICAL(&s_dht_spinlock);

//     if (err != ESP_OK) {
//         ESP_LOGW(TAG, "DHT22 read timeout/error during bit capture");
//         return err;
//     }

//     // ------------------------------------------------------------------
//     // 4. Checksum verification
//     // ------------------------------------------------------------------
//     uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
//     if (checksum != data[4]) {
//         ESP_LOGW(TAG, "DHT22 checksum error (got 0x%02X, expected 0x%02X)",
//         data[4], checksum); return ESP_ERR_INVALID_CRC;
//     }

//     // ------------------------------------------------------------------
//     // 5. Decode: DHT22 returns 16-bit fixed-point (÷10)
//     //    Byte 0-1: humidity × 10
//     //    Byte 2-3: temperature × 10  (bit15 of byte2 = sign)
//     // ------------------------------------------------------------------
//     *humidity    = (float)((data[0] << 8) | data[1]) * 0.1f;
//     int16_t raw  = (int16_t)(((data[2] & 0x7F) << 8) | data[3]);
//     *temperature = raw * 0.1f * ((data[2] & 0x80) ? -1.0f : 1.0f);

//     // Thread-safe update of cached values
//     if (s_dht_mutex && xSemaphoreTake(s_dht_mutex, pdMS_TO_TICKS(100)) ==
//     pdTRUE) {
//         s_last_temp = *temperature;
//         s_last_hum  = *humidity;
//         xSemaphoreGive(s_dht_mutex);
//     }

//     return ESP_OK;
// }

// float dht_sensor_get_temperature(void) {
//     float val = 0.0f;
//     if (s_dht_mutex && xSemaphoreTake(s_dht_mutex, pdMS_TO_TICKS(100)) ==
//     pdTRUE) {
//         val = s_last_temp;
//         xSemaphoreGive(s_dht_mutex);
//     }
//     return val;
// }

// float dht_sensor_get_humidity(void) {
//     float val = 0.0f;
//     if (s_dht_mutex && xSemaphoreTake(s_dht_mutex, pdMS_TO_TICKS(100)) ==
//     pdTRUE) {
//         val = s_last_hum;
//         xSemaphoreGive(s_dht_mutex);
//     }
//     return val;
// }
