#include "ac_timer_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "command_dispatcher.h"
#include <string.h>

static const char *TAG = "AC_TIMER";

#include "freertos/semphr.h"

static int timer_remaining = 0;
static cJSON *target_ir_code = NULL;
static SemaphoreHandle_t s_timer_mutex = NULL;

// Provided by firebase_manager
extern void firebase_trigger_update(void);

static void ac_timer_task(void *pvParameters) {
    while (1) {
        cJSON *cmd_to_exec = NULL;
        
        if (s_timer_mutex && xSemaphoreTake(s_timer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (timer_remaining > 0) {
                timer_remaining--;
                
                if (timer_remaining == 0) {
                    ESP_LOGI(TAG, "\033[1;33mTimer expired! Sending AC OFF IR code...\033[0m");
                    if (target_ir_code != NULL) {
                        // Create a synthetic JSON command for ir_send
                        cJSON *cmd = cJSON_CreateObject();
                        if (cmd) {
                            cJSON_AddStringToObject(cmd, "action", "ir_send");
                            
                            cJSON *protocol = cJSON_GetObjectItem(target_ir_code, "protocol");
                            cJSON *value = cJSON_GetObjectItem(target_ir_code, "value");
                            cJSON *bits = cJSON_GetObjectItem(target_ir_code, "bits");
                            cJSON *frequency = cJSON_GetObjectItem(target_ir_code, "frequency");
                            
                            if (protocol && cJSON_IsString(protocol)) cJSON_AddStringToObject(cmd, "protocol", protocol->valuestring);
                            if (value && cJSON_IsString(value)) cJSON_AddStringToObject(cmd, "value", value->valuestring);
                            if (bits && cJSON_IsNumber(bits)) cJSON_AddNumberToObject(cmd, "bits", bits->valueint);
                            if (frequency && cJSON_IsNumber(frequency)) cJSON_AddNumberToObject(cmd, "frequency", frequency->valueint);
                            
                            cmd_to_exec = cmd;
                        }
                    }
                    
                    // Cleanup
                    if (target_ir_code) {
                        cJSON_Delete(target_ir_code);
                        target_ir_code = NULL;
                    }
                }
            }
            xSemaphoreGive(s_timer_mutex);
        }
        
        if (cmd_to_exec) {
            command_dispatcher_execute(cmd_to_exec);
            cJSON_Delete(cmd_to_exec);
            // Trigger state sync so Flutter sees ac_timer_remaining is 0
            firebase_trigger_update();
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void ac_timer_manager_init(void) {
    ESP_LOGI(TAG, "\033[1;36m[AC TIMER]\033[0m Initializing Autonomous Timer...");
    s_timer_mutex = xSemaphoreCreateMutex();
    // 4096 stack size should be plenty
    xTaskCreate(ac_timer_task, "ac_timer_task", 4096, NULL, 4, NULL);
}

void ac_timer_set(int seconds, const cJSON *ir_code_json) {
    if (seconds <= 0) {
        ac_timer_cancel();
        return;
    }

    if (s_timer_mutex && xSemaphoreTake(s_timer_mutex, portMAX_DELAY) == pdTRUE) {
        if (target_ir_code != NULL) {
            cJSON_Delete(target_ir_code);
            target_ir_code = NULL;
        }

        if (ir_code_json != NULL) {
            target_ir_code = cJSON_Duplicate(ir_code_json, 1);
        } else {
            ESP_LOGW(TAG, "ac_timer_set called with NULL ir_code_json");
        }
        
        timer_remaining = seconds;
        ESP_LOGI(TAG, "\033[1;32mAC Timer set for %d seconds\033[0m", seconds);
        xSemaphoreGive(s_timer_mutex);
    }
}

void ac_timer_cancel(void) {
    if (s_timer_mutex && xSemaphoreTake(s_timer_mutex, portMAX_DELAY) == pdTRUE) {
        if (timer_remaining > 0) {
            ESP_LOGI(TAG, "\033[1;31mAC Timer cancelled.\033[0m");
        }
        timer_remaining = 0;
        if (target_ir_code != NULL) {
            cJSON_Delete(target_ir_code);
            target_ir_code = NULL;
        }
        xSemaphoreGive(s_timer_mutex);
    }
}

int ac_timer_get_remaining(void) {
    int rem = 0;
    if (s_timer_mutex && xSemaphoreTake(s_timer_mutex, portMAX_DELAY) == pdTRUE) {
        rem = timer_remaining;
        xSemaphoreGive(s_timer_mutex);
    }
    return rem;
}
