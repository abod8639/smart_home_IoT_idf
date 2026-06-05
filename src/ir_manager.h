#ifndef IR_MANAGER_H
#define IR_MANAGER_H

#include <stdint.h>
#include <stddef.h>

#define IR_TX_PIN 33
#define IR_RX_PIN 32

void ir_manager_init(void);
void ir_send_raw(uint16_t* durations, size_t length, uint32_t freq_hz);
void ir_manager_start_learning(void);

#endif // IR_MANAGER_H
