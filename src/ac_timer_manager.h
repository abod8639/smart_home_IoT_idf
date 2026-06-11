#ifndef AC_TIMER_MANAGER_H
#define AC_TIMER_MANAGER_H

#include "cJSON.h"

void ac_timer_manager_init(void);
void ac_timer_set(int seconds, const cJSON *ir_code_json);
void ac_timer_cancel(void);
int ac_timer_get_remaining(void);

#endif // AC_TIMER_MANAGER_H
