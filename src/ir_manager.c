#include "ir_manager.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"

static const char *TAG = "IR_MANAGER";

void ir_manager_init(void) {
    ESP_LOGI(TAG, "Initializing IR Manager (RMT)");
    // TODO: Initialize RX channel if needed for learning
}

void ir_send_raw(uint16_t* durations, size_t length, uint32_t freq_hz) {
    ESP_LOGI(TAG, "Sending IR Raw Data");
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = IR_TX_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1us precision
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    rmt_channel_handle_t tx_chan = NULL;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_chan));

    rmt_carrier_config_t carrier_config = {
        .frequency_hz = freq_hz,
        .duty_cycle = 0.33,
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(tx_chan, &carrier_config));
    ESP_ERROR_CHECK(rmt_enable(tx_chan));

    rmt_symbol_word_t* rmt_data = malloc(sizeof(rmt_symbol_word_t) * length);
    if (!rmt_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for RMT data");
        rmt_disable(tx_chan);
        rmt_del_channel(tx_chan);
        return;
    }

    for (size_t i = 0; i < length; i++) {
        rmt_data[i] = (rmt_symbol_word_t) {
            .duration0 = durations[i],
            .level0 = (i % 2 == 0) ? 1 : 0,
            .duration1 = 0,
            .level1 = 0
        };
    }

    rmt_encoder_handle_t copy_encoder = NULL;
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &copy_encoder));

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    rmt_transmit(tx_chan, copy_encoder, rmt_data, sizeof(rmt_symbol_word_t) * length, &tx_config);
    // Wait for transmission to complete
    rmt_tx_wait_all_done(tx_chan, -1);

    free(rmt_data);
    rmt_del_encoder(copy_encoder);
    rmt_disable(tx_chan);
    rmt_del_channel(tx_chan);
}

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "ws_server.h"
#include "rom/ets_sys.h"

static void ir_rx_task(void *pvParameters) {
    ESP_LOGI(TAG, "IR learning started, listening on pin %d...", IR_RX_PIN);
    gpio_set_direction((gpio_num_t)IR_RX_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)IR_RX_PIN, GPIO_PULLUP_ONLY);

    // Wait for the pin to go low (start of transmission)
    int timeout_ms = 10000;
    int elapsed_ms = 0;
    while (gpio_get_level((gpio_num_t)IR_RX_PIN) == 1 && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed_ms += 10;
    }

    if (elapsed_ms >= timeout_ms) {
        ESP_LOGI(TAG, "IR learning timeout - no signal detected");
        ws_server_broadcast("{\"event\":\"ir_learn_status\",\"status\":\"timeout\"}");
        vTaskDelete(NULL);
        return;
    }

    // Capture transitions
    uint16_t *raw_buf = malloc(sizeof(uint16_t) * 128);
    if (!raw_buf) {
        vTaskDelete(NULL);
        return;
    }
    int count = 0;
    uint32_t last_time = esp_timer_get_time();
    int last_level = 0;

    // Capture for up to 100ms or 128 transitions
    uint32_t start_capture = esp_timer_get_time();
    while (count < 128 && (esp_timer_get_time() - start_capture) < 100000) {
        int level = gpio_get_level((gpio_num_t)IR_RX_PIN);
        if (level != last_level) {
            uint32_t now = esp_timer_get_time();
            uint32_t diff = now - last_time;
            if (diff > 100) { // filter noise
                raw_buf[count++] = diff;
                last_time = now;
                last_level = level;
            }
        }
        ets_delay_us(10);
    }

    if (count > 10) {
        // Format as comma separated decimal string
        char *val_str = malloc(count * 8 + 1);
        val_str[0] = '\0';
        for (int i = 0; i < count; i++) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%d%s", raw_buf[i], (i == count - 1) ? "" : ",");
            strcat(val_str, tmp);
        }

        // Broadcast raw capture
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "event", "ir_learn_status");
        cJSON_AddStringToObject(root, "status", "ok");
        cJSON_AddStringToObject(root, "protocol", "RAW");
        cJSON_AddStringToObject(root, "value", val_str);
        cJSON_AddNumberToObject(root, "bits", count);
        cJSON_AddNumberToObject(root, "frequency", 38);

        char *json_str = cJSON_PrintUnformatted(root);
        ws_server_broadcast(json_str);

        free(json_str);
        cJSON_Delete(root);
        free(val_str);
    } else {
        ws_server_broadcast("{\"event\":\"ir_learn_status\",\"status\":\"error\",\"message\":\"Signal too short\"}");
    }

    free(raw_buf);
    vTaskDelete(NULL);
}

void ir_manager_start_learning(void) {
    xTaskCreate(ir_rx_task, "ir_rx_task", 4096, NULL, 5, NULL);
}

