#include "ir_manager.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include "cJSON.h"
#include "ws_server.h"
#include "firebase_manager.h"

static const char *TAG = "IR_MANAGER";

// ---------------------------------------------------------------------------
// Persistent RMT TX resources — created once in ir_manager_init().
// Avoids the ~150 ms create/delete overhead on every IR send.
// ---------------------------------------------------------------------------
static rmt_channel_handle_t s_tx_chan     = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;

// Guard against launching multiple concurrent learn tasks.
static TaskHandle_t s_ir_rx_task_handle = NULL;

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void ir_manager_init(void) {
    ESP_LOGI(TAG, "Initializing IR Manager (TX pin %d | RX pin %d)", IR_TX_PIN, IR_RX_PIN);

    // Create the TX channel once — reused for every ir_send_raw() call.
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num          = IR_TX_PIN,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 1000000, // 1 µs resolution
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    if (rmt_new_tx_channel(&tx_cfg, &s_tx_chan) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel");
        return;
    }

    // Default carrier: 38 kHz, duty 33 % (standard NEC / most IR protocols)
    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = 38000,
        .duty_cycle   = 0.33f,
    };
    rmt_apply_carrier(s_tx_chan, &carrier_cfg);
    rmt_enable(s_tx_chan);

    // Create copy encoder once — reused for every transmission.
    rmt_copy_encoder_config_t enc_cfg = {};
    if (rmt_new_copy_encoder(&enc_cfg, &s_copy_encoder) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT copy encoder");
        rmt_disable(s_tx_chan);
        rmt_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return;
    }

    ESP_LOGI(TAG, "IR Manager initialized");
}

// ---------------------------------------------------------------------------
// IR Transmission
// ---------------------------------------------------------------------------

void ir_send_raw(uint16_t *durations, size_t length, uint32_t freq_hz) {
    if (!s_tx_chan || !s_copy_encoder) {
        ESP_LOGE(TAG, "IR TX not initialised — call ir_manager_init() first");
        return;
    }

    ESP_LOGI(TAG, "\033[1;35m[IR]\033[0m Sending %zu symbols @ %lu Hz", length, (unsigned long)freq_hz);

    rmt_symbol_word_t *rmt_data = malloc(sizeof(rmt_symbol_word_t) * length);
    if (!rmt_data) {
        ESP_LOGE(TAG, "Failed to allocate RMT symbol buffer");
        return;
    }

    for (size_t i = 0; i < length; i++) {
        rmt_data[i] = (rmt_symbol_word_t){
            .duration0 = durations[i],
            .level0    = (i % 2 == 0) ? 1 : 0,
            .duration1 = 0,
            .level1    = 0,
        };
    }

    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    rmt_transmit(s_tx_chan, s_copy_encoder, rmt_data,
                 sizeof(rmt_symbol_word_t) * length, &tx_config);
    rmt_tx_wait_all_done(s_tx_chan, -1); // Block until transmission complete

    free(rmt_data);
}

// ---------------------------------------------------------------------------
// IR Learning Task
// ---------------------------------------------------------------------------

static void ir_rx_task(void *pvParameters) {
    ESP_LOGI(TAG, "\033[1;35m[IR]\033[0m Learning mode started — listening on GPIO %d...", IR_RX_PIN);
    gpio_set_direction((gpio_num_t)IR_RX_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)IR_RX_PIN, GPIO_PULLUP_ONLY);

    // Wait for the pin to go LOW (start of IR burst), 10 s timeout
    int timeout_ms = 10000;
    int elapsed_ms = 0;
    while (gpio_get_level((gpio_num_t)IR_RX_PIN) == 1 && elapsed_ms < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed_ms += 10;
    }

    if (elapsed_ms >= timeout_ms) {
        ESP_LOGW(TAG, "IR learning timeout — no signal detected");
        ws_server_broadcast("{\"event\":\"ir_learn_status\",\"status\":\"timeout\"}");
        s_ir_rx_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Capture transitions (up to 128) for at most 100 ms
    uint16_t *raw_buf = malloc(sizeof(uint16_t) * 128);
    if (!raw_buf) {
        s_ir_rx_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int count = 0;
    int last_level = 0;
    uint32_t last_time = (uint32_t)esp_timer_get_time();
    uint32_t start_capture = last_time;

    while (count < 128 && ((uint32_t)esp_timer_get_time() - start_capture) < 100000U) {
        int level = gpio_get_level((gpio_num_t)IR_RX_PIN);
        if (level != last_level) {
            uint32_t now  = (uint32_t)esp_timer_get_time();
            uint32_t diff = now - last_time;
            if (diff > 100) { // Ignore glitches < 100 µs
                raw_buf[count++] = (uint16_t)(diff > 0xFFFF ? 0xFFFF : diff);
                last_time  = now;
                last_level = level;
            }
        }
        ets_delay_us(10);
    }

    if (count > 10) {
        // Build comma-separated string safely using snprintf + offset tracking.
        // uint16_t max = 65535 (5 digits) + comma = 6 chars per sample.
        int buf_size = count * 7 + 1;
        char *val_str = malloc(buf_size);
        if (!val_str) goto cleanup;

        int offset = 0;
        for (int i = 0; i < count; i++) {
            offset += snprintf(val_str + offset, buf_size - offset,
                               "%u%s", (unsigned)raw_buf[i], (i < count - 1) ? "," : "");
        }

        // Broadcast result via WebSocket
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "event",    "ir_learn_status");
        cJSON_AddStringToObject(root, "status",   "ok");
        cJSON_AddStringToObject(root, "protocol", "RAW");
        cJSON_AddStringToObject(root, "value",    val_str);
        cJSON_AddNumberToObject(root, "bits",     count);
        cJSON_AddNumberToObject(root, "frequency", 38);

        char *json_str = cJSON_PrintUnformatted(root);
        ws_server_broadcast(json_str);
        firebase_update_ir_signal("RAW", val_str);

        free(json_str);
        cJSON_Delete(root);
        free(val_str);
    } else {
        ws_server_broadcast("{\"event\":\"ir_learn_status\",\"status\":\"error\","
                            "\"message\":\"Signal too short\"}");
    }

cleanup:
    free(raw_buf);
    s_ir_rx_task_handle = NULL; // Allow a new learning session
    vTaskDelete(NULL);
}

void ir_manager_start_learning(void) {
    if (s_ir_rx_task_handle != NULL) {
        ESP_LOGW(TAG, "IR learning already in progress — ignoring duplicate request");
        return;
    }
    xTaskCreate(ir_rx_task, "ir_rx_task", 4096, NULL, 5, &s_ir_rx_task_handle);
}
