#ifndef IR_MANAGER_H
#define IR_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include "device_config.h"   // Pin definitions (IR_TX_PIN, IR_RX_PIN)

void ir_manager_init(void);
void ir_send_raw(uint16_t* durations, size_t length, uint32_t freq_hz);
void ir_manager_start_learning(void);

#endif // IR_MANAGER_H
