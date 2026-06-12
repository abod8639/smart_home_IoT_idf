#ifndef FREERTOS_H
#define FREERTOS_H

#include <stdint.h>
#include <stddef.h>

#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY ((uint32_t)-1)
#define pdTRUE 1
#define pdFALSE 0

typedef void* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef uint32_t TickType_t;

#endif // FREERTOS_H
