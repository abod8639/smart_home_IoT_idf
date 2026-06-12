#ifndef MOCK_STATUS_H
#define MOCK_STATUS_H

#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

typedef struct {
    struct {
        int call_count;
        int last_pin;
        int last_state;
        int return_val[32]; // Return value map indexed by pin if needed
    } gpio;
    
    struct {
        int call_count;
        int last_pin;
        uint32_t last_duty;
        uint32_t return_val[32]; // Return value map indexed by pin
    } pwm;
    
    struct {
        int get_call_count;
        int save_call_count;
        int last_saved_temp;
        int get_return_val;
    } nvs;
    
    struct {
        int send_raw_call_count;
        uint16_t last_durations[512];
        size_t last_count;
        uint32_t last_frequency;
        
        int start_learning_call_count;
    } ir;
    
    struct {
        int start_call_count;
        char last_url[256];
    } ota;
    
    struct {
        int publish_call_count;
        char last_published_event[256];
    } mqtt;
    
    struct {
        int set_call_count;
        int cancel_call_count;
        int last_seconds;
        cJSON *last_ir_code;
        int remaining_return_val;
    } ac_timer;
    
    struct {
        int trigger_update_call_count;
    } firebase;

    struct {
        int get_rssi_return_val;
    } wifi;

    struct {
        float temperature_return_val;
        float humidity_return_val;
    } dht;
    
    struct {
        uint32_t free_heap_return_val;
    } system;
} mock_status_t;

extern mock_status_t mock_status;

void mock_status_reset(void);

#endif // MOCK_STATUS_H
