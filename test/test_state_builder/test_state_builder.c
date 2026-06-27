#include "unity.h"
#include "state_builder.h"
#include "device_config.h"
#include "../mocks/mock_status.h"
#include <stdlib.h>


// Include mock implementations to build them in this compilation unit
#include "../mocks/mocks.c"

void setUp(void) {
    mock_status_reset();
}

void tearDown(void) {
    // No cleanup required
}

void test_state_builder_create_full(void) {
    // Configure mock telemetry values
    mock_status.dht.temperature_return_val = 26.5f;
    mock_status.dht.humidity_return_val = 60.2f;
    mock_status.nvs.get_return_val = 23;
    mock_status.ac_timer.remaining_return_val = 120;
    mock_status.wifi.get_rssi_return_val = -65;
    mock_status.system.free_heap_return_val = 180000;
    
    // Configure mock relay pin states
    mock_status.gpio.return_val[RELAY_1_PIN] = 1;
    mock_status.gpio.return_val[RELAY_2_PIN] = 0;
    mock_status.gpio.return_val[RELAY_3_PIN] = 1;
    mock_status.gpio.return_val[RELAY_4_PIN] = 0;
    
    // Configure mock PWM duty values
    mock_status.pwm.return_val[PWM_LAMP_PIN] = 100;
    mock_status.pwm.return_val[PWM_RGB_R_PIN] = 50;
    mock_status.pwm.return_val[PWM_RGB_G_PIN] = 150;
    mock_status.pwm.return_val[PWM_RGB_B_PIN] = 250;
    
    // Run the state builder
    cJSON *root = state_builder_create_full();
    TEST_ASSERT_NOT_NULL(root);
    
    // Assert root-level parameters (Note: temperature and humidity are disabled)
    /*
    cJSON *temp = cJSON_GetObjectItem(root, "temperature");
    TEST_ASSERT_NOT_NULL(temp);
    TEST_ASSERT_EQUAL_FLOAT(26.5f, temp->valuedouble);
    
    cJSON *hum = cJSON_GetObjectItem(root, "humidity");
    TEST_ASSERT_NOT_NULL(hum);
    TEST_ASSERT_EQUAL_FLOAT(60.2f, hum->valuedouble);
    */
    
    cJSON *tgt = cJSON_GetObjectItem(root, "target_temperature");
    TEST_ASSERT_NOT_NULL(tgt);
    TEST_ASSERT_EQUAL(23, tgt->valueint);
    
    cJSON *rem = cJSON_GetObjectItem(root, "ac_timer_remaining");
    TEST_ASSERT_NOT_NULL(rem);
    TEST_ASSERT_EQUAL(120, rem->valueint);
    
    cJSON *rssi = cJSON_GetObjectItem(root, "wifi_rssi");
    TEST_ASSERT_NOT_NULL(rssi);
    TEST_ASSERT_EQUAL(-65, rssi->valueint);
    
    cJSON *heap = cJSON_GetObjectItem(root, "heap_free");
    TEST_ASSERT_NOT_NULL(heap);
    TEST_ASSERT_EQUAL(180000, heap->valueint);
    
    // Assert nested pins object
    cJSON *pins = cJSON_GetObjectItem(root, "pins");
    TEST_ASSERT_NOT_NULL(pins);
    
    // Assert relay states
    cJSON *r1 = cJSON_GetObjectItem(pins, "relay_1");
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_EQUAL(1, r1->valueint);
    
    cJSON *r2 = cJSON_GetObjectItem(pins, "relay_2");
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_EQUAL(0, r2->valueint);
    
    cJSON *r3 = cJSON_GetObjectItem(pins, "relay_3");
    TEST_ASSERT_NOT_NULL(r3);
    TEST_ASSERT_EQUAL(1, r3->valueint);
    
    cJSON *r4 = cJSON_GetObjectItem(pins, "relay_4");
    TEST_ASSERT_NOT_NULL(r4);
    TEST_ASSERT_EQUAL(0, r4->valueint);
    
    // Assert PWM duty values
    cJSON *lamp = cJSON_GetObjectItem(pins, "pwm_lamp");
    TEST_ASSERT_NOT_NULL(lamp);
    TEST_ASSERT_EQUAL(100, lamp->valueint);
    
    cJSON *rgb_r = cJSON_GetObjectItem(pins, "pwm_rgb_r");
    TEST_ASSERT_NOT_NULL(rgb_r);
    TEST_ASSERT_EQUAL(50, rgb_r->valueint);
    
    cJSON *rgb_g = cJSON_GetObjectItem(pins, "pwm_rgb_g");
    TEST_ASSERT_NOT_NULL(rgb_g);
    TEST_ASSERT_EQUAL(150, rgb_g->valueint);
    
    cJSON *rgb_b = cJSON_GetObjectItem(pins, "pwm_rgb_b");
    TEST_ASSERT_NOT_NULL(rgb_b);
    TEST_ASSERT_EQUAL(250, rgb_b->valueint);
    
    // Clean up
    cJSON_Delete(root);
}

void test_state_builder_create_json_string(void) {
    // Configure mock telemetry values
    mock_status.dht.temperature_return_val = 21.0f;
    mock_status.dht.humidity_return_val = 50.0f;
    mock_status.nvs.get_return_val = 24;
    mock_status.ac_timer.remaining_return_val = 0;
    mock_status.wifi.get_rssi_return_val = -70;
    mock_status.system.free_heap_return_val = 200000;
    
    char *json_str = state_builder_create_json_string();
    TEST_ASSERT_NOT_NULL(json_str);
    
    // Parse the generated JSON string back to verify it works and is valid
    cJSON *parsed = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(parsed);
    
    /*
    cJSON *temp = cJSON_GetObjectItem(parsed, "temperature");
    TEST_ASSERT_NOT_NULL(temp);
    TEST_ASSERT_EQUAL_FLOAT(21.0f, temp->valuedouble);
    */
    
    cJSON *heap = cJSON_GetObjectItem(parsed, "heap_free");
    TEST_ASSERT_NOT_NULL(heap);
    TEST_ASSERT_EQUAL(200000, heap->valueint);
    
    // Clean up
    cJSON_Delete(parsed);
    free(json_str);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_state_builder_create_full);
    RUN_TEST(test_state_builder_create_json_string);
    
    return UNITY_END();
}
