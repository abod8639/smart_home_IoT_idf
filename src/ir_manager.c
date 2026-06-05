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
#include "mqtt_manager.h"
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

static bool IRAM_ATTR rmt_rx_done_callback(
    rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event_data,
    void *user_data) {
  TaskHandle_t task_handle = (TaskHandle_t)user_data;
  BaseType_t high_task_wakeup = pdFALSE;
  xTaskNotifyFromISR(task_handle, event_data->num_symbols,
                     eSetValueWithOverwrite, &high_task_wakeup);
  return high_task_wakeup == pdTRUE;
}

static void ir_rx_task(void *pvParameters) {
  ESP_LOGI(
      TAG,
      "\033[1;35m[IR]\033[0m Learning mode started — listening on GPIO %d...",
      IR_RX_PIN);

  // 1. Configure RMT RX Channel
  rmt_channel_handle_t rx_chan = NULL;
  rmt_rx_channel_config_t rx_config = {
      .gpio_num = (gpio_num_t)IR_RX_PIN,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 1000000, // 1 MHz (1 tick = 1 microsecond)
      .mem_block_symbols = 64,  // 64 symbols = up to 128 transitions
      .flags = {
          .invert_in = true, // Invert input so active-low IR receiver pulse is
                             // seen as active-high (1)
      }};

  if (rmt_new_rx_channel(&rx_config, &rx_chan) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create RMT RX channel");
    mqtt_manager_publish_event(
        "{\"event\":\"ir_learn_status\",\"status\":\"error\",\"message\":"
        "\"Failed to init RMT RX\"}");
    s_ir_rx_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }

  // 2. Register callback to notify this task when reception is complete
  rmt_rx_event_callbacks_t cbs = {
      .on_recv_done = rmt_rx_done_callback,
  };
  if (rmt_rx_register_event_callbacks(rx_chan, &cbs,
                                      xTaskGetCurrentTaskHandle()) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register RMT RX callbacks");
    rmt_del_channel(rx_chan);
    mqtt_manager_publish_event(
        "{\"event\":\"ir_learn_status\",\"status\":\"error\",\"message\":"
        "\"Failed to register callback\"}");
    s_ir_rx_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }

  // 3. Enable RMT RX channel
  if (rmt_enable(rx_chan) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable RMT RX channel");
    rmt_del_channel(rx_chan);
    s_ir_rx_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }

  // 4. Start receiving
  rmt_symbol_word_t receive_buffer[64];
  rmt_receive_config_t receive_config = {
      .signal_range_min_ns = 1000,    // ignore pulses shorter than 1us (glitch filter)
      .signal_range_max_ns = 30000000, // 30ms of silence terminates capture (end of IR frame)
  };

  if (rmt_receive(rx_chan, receive_buffer, sizeof(receive_buffer),
                  &receive_config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start RMT RX receive");
    rmt_disable(rx_chan);
    rmt_del_channel(rx_chan);
    s_ir_rx_task_handle = NULL;
    vTaskDelete(NULL);
    return;
  }

  // 5. Wait for notification (from ISR callback) with a 10s timeout
  uint32_t num_symbols = 0;
  BaseType_t notify_recv =
      xTaskNotifyWait(0, 0, &num_symbols, pdMS_TO_TICKS(10000));

  if (notify_recv == pdFALSE) {
    ESP_LOGW(TAG, "IR learning timeout — no signal detected within 10s");
    mqtt_manager_publish_event(
        "{\"event\":\"ir_learn_status\",\"status\":\"timeout\"}");
  } else {
    ESP_LOGI(TAG, "IR signal captured! Received %lu symbols",
             (unsigned long)num_symbols);
    if (num_symbols > 5) {
      // Convert RMT symbols to comma-separated microseconds durations
      int max_durations = num_symbols * 2;
      uint16_t *raw_buf = malloc(sizeof(uint16_t) * max_durations);
      if (!raw_buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for raw durations");
        mqtt_manager_publish_event("{\"event\":\"ir_learn_status\",\"status\":"
                                   "\"error\",\"message\":\"Out of memory\"}");
        goto cleanup;
      }

      int count = 0;
      for (uint32_t i = 0; i < num_symbols; i++) {
        rmt_symbol_word_t sym = receive_buffer[i];
        if (sym.duration0 > 0) {
          raw_buf[count++] = sym.duration0;
        }
        if (sym.duration1 > 0) {
          raw_buf[count++] = sym.duration1;
        }
      }

      // Build comma-separated string safely
      int buf_size = count * 7 + 1;
      char *val_str = malloc(buf_size);
      if (!val_str) {
        free(raw_buf);
        mqtt_manager_publish_event("{\"event\":\"ir_learn_status\",\"status\":"
                                   "\"error\",\"message\":\"Out of memory\"}");
        goto cleanup;
      }

      int offset = 0;
      for (int i = 0; i < count; i++) {
        offset += snprintf(val_str + offset, buf_size - offset, "%u%s",
                           (unsigned)raw_buf[i], (i < count - 1) ? "," : "");
      }

      // Broadcast result via MQTT and Firebase
      cJSON *root = cJSON_CreateObject();
      cJSON_AddStringToObject(root, "event", "ir_learn_status");
      cJSON_AddStringToObject(root, "status", "ok");
      cJSON_AddStringToObject(root, "protocol", "RAW");
      cJSON_AddStringToObject(root, "value", val_str);
      cJSON_AddNumberToObject(root, "bits", count);
      cJSON_AddNumberToObject(root, "frequency", 38);

      char *json_str = cJSON_PrintUnformatted(root);
      mqtt_manager_publish_event(json_str);
      firebase_update_ir_signal("RAW", val_str);

      free(json_str);
      cJSON_Delete(root);
      free(val_str);
      free(raw_buf);
    } else {
      mqtt_manager_publish_event("{\"event\":\"ir_learn_status\",\"status\":"
                                 "\"error\",\"message\":\"Signal too short\"}");
    }
  }

cleanup:
  // 6. Cleanup RMT channel
  rmt_disable(rx_chan);
  rmt_del_channel(rx_chan);

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
