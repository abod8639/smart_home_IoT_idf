#include "unity.h"
#include "command_dispatcher.h"
#include "device_config.h"
#include "../mocks/mock_status.h"

// Include mock implementations to build them in this compilation unit
#include "../mocks/mocks.c"

void setUp(void) {
    mock_status_reset();
}

void tearDown(void) {
    // No cleanup required
}

// 1. Test null JSON inputs
void test_dispatcher_null_json(void) {
    esp_err_t err = command_dispatcher_execute(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

// 2. Test JSON inputs missing the "action" field
void test_dispatcher_missing_action(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "pin", 2);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    cJSON_Delete(json);
}

// 3. Test unknown actions
void test_dispatcher_unknown_action(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "invalid_action_name");
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
    
    cJSON_Delete(json);
}

// 4. Test set_relay with valid pin and values
void test_set_relay_success_on(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "set_relay");
    cJSON_AddNumberToObject(json, "pin", RELAY_1_PIN);
    cJSON_AddNumberToObject(json, "value", 1);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Verify mocks
    TEST_ASSERT_EQUAL(1, mock_status.gpio.call_count);
    TEST_ASSERT_EQUAL(RELAY_1_PIN, mock_status.gpio.last_pin);
    TEST_ASSERT_EQUAL(1, mock_status.gpio.last_state);
    
    TEST_ASSERT_EQUAL(1, mock_status.mqtt.publish_call_count);
    TEST_ASSERT_EQUAL_STRING("{\"event\":\"relay_update\",\"endpoint\":1,\"state\":1}", mock_status.mqtt.last_published_event);
    
    TEST_ASSERT_EQUAL(1, mock_status.firebase.trigger_update_call_count);
    
    cJSON_Delete(json);
}

void test_set_relay_success_off(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "set_relay");
    cJSON_AddNumberToObject(json, "pin", RELAY_2_PIN);
    cJSON_AddNumberToObject(json, "value", 0);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(1, mock_status.gpio.call_count);
    TEST_ASSERT_EQUAL(RELAY_2_PIN, mock_status.gpio.last_pin);
    TEST_ASSERT_EQUAL(0, mock_status.gpio.last_state);
    
    TEST_ASSERT_EQUAL(1, mock_status.mqtt.publish_call_count);
    TEST_ASSERT_EQUAL_STRING("{\"event\":\"relay_update\",\"endpoint\":2,\"state\":0}", mock_status.mqtt.last_published_event);
    
    cJSON_Delete(json);
}

// 5. Test set_relay with invalid pin number
void test_set_relay_invalid_pin(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "set_relay");
    cJSON_AddNumberToObject(json, "pin", 99); // Invalid Pin
    cJSON_AddNumberToObject(json, "value", 1);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    TEST_ASSERT_EQUAL(0, mock_status.gpio.call_count);
    
    cJSON_Delete(json);
}

// 6. Test set_relay with missing params
void test_set_relay_missing_params(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "set_relay");
    cJSON_AddNumberToObject(json, "pin", RELAY_1_PIN);
    // Missing value
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    cJSON_Delete(json);
}

// 7. Test set_pwm with valid parameters and clamping
void test_set_pwm_success(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "set_pwm");
    cJSON_AddNumberToObject(json, "pin", PWM_LAMP_PIN);
    cJSON_AddNumberToObject(json, "value", 128);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(1, mock_status.pwm.call_count);
    TEST_ASSERT_EQUAL(PWM_LAMP_PIN, mock_status.pwm.last_pin);
    TEST_ASSERT_EQUAL(128, mock_status.pwm.last_duty);
    
    TEST_ASSERT_EQUAL(1, mock_status.mqtt.publish_call_count);
    TEST_ASSERT_EQUAL_STRING("{\"event\":\"pwm_update\",\"endpoint\":5,\"level\":128}", mock_status.mqtt.last_published_event);
    
    cJSON_Delete(json);
}

// 8. Test set_pwm clamping behavior (below 0 and above 255)
void test_set_pwm_clamping_low(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "set_pwm");
    cJSON_AddNumberToObject(json, "pin", PWM_RGB_R_PIN);
    cJSON_AddNumberToObject(json, "value", -50);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(0, mock_status.pwm.last_duty); // Clamped to 0
    cJSON_Delete(json);
}

void test_set_pwm_clamping_high(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "set_pwm");
    cJSON_AddNumberToObject(json, "pin", PWM_RGB_R_PIN);
    cJSON_AddNumberToObject(json, "value", 300);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(255, mock_status.pwm.last_duty); // Clamped to 255
    cJSON_Delete(json);
}

// 9. Test set_pwm with invalid pin
void test_set_pwm_invalid_pin(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "set_pwm");
    cJSON_AddNumberToObject(json, "pin", 99);
    cJSON_AddNumberToObject(json, "value", 100);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    cJSON_Delete(json);
}

// 10. Test control_ac command success (Power On / Temp Setting)
void test_control_ac_success_on(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "control_ac");
    cJSON_AddBoolToObject(json, "isOn", true);
    cJSON_AddNumberToObject(json, "target_temp", 22);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(1, mock_status.nvs.save_call_count);
    TEST_ASSERT_EQUAL(22, mock_status.nvs.last_saved_temp);
    
    TEST_ASSERT_EQUAL(1, mock_status.gpio.call_count);
    TEST_ASSERT_EQUAL(RELAY_3_PIN, mock_status.gpio.last_pin);
    TEST_ASSERT_EQUAL(1, mock_status.gpio.last_state);
    
    TEST_ASSERT_EQUAL(1, mock_status.mqtt.publish_call_count);
    TEST_ASSERT_EQUAL_STRING("{\"event\":\"ac_update\",\"isOn\":true,\"target_temp\":22}", mock_status.mqtt.last_published_event);
    
    cJSON_Delete(json);
}

// 11. Test control_ac power off cancels the AC timer
void test_control_ac_power_off_cancels_timer(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "control_ac");
    cJSON_AddBoolToObject(json, "isOn", false);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(1, mock_status.ac_timer.cancel_call_count);
    
    cJSON_Delete(json);
}

// 12. Test control_ac temperature clamping (between 16 and 30)
void test_control_ac_temp_clamping_low(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "control_ac");
    cJSON_AddNumberToObject(json, "target_temp", 10);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(16, mock_status.nvs.last_saved_temp); // Clamped to 16
    cJSON_Delete(json);
}

void test_control_ac_temp_clamping_high(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "control_ac");
    cJSON_AddNumberToObject(json, "target_temp", 45);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(30, mock_status.nvs.last_saved_temp); // Clamped to 30
    cJSON_Delete(json);
}

// 13. Test set_ac_timer
void test_set_ac_timer_success(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "set_ac_timer");
    cJSON_AddNumberToObject(json, "seconds", 3600);
    
    cJSON *ir_code = cJSON_CreateObject();
    cJSON_AddStringToObject(ir_code, "protocol", "NEC");
    cJSON_AddStringToObject(ir_code, "value", "A55A");
    cJSON_AddItemToObject(json, "ir_code", ir_code);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(1, mock_status.ac_timer.set_call_count);
    TEST_ASSERT_EQUAL(3600, mock_status.ac_timer.last_seconds);
    TEST_ASSERT_NOT_NULL(mock_status.ac_timer.last_ir_code);
    
    cJSON *last_proto = cJSON_GetObjectItem(mock_status.ac_timer.last_ir_code, "protocol");
    TEST_ASSERT_EQUAL_STRING("NEC", last_proto->valuestring);
    
    cJSON_Delete(json);
}

// 14. Test ir_send RAW protocol
void test_ir_send_raw(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "ir_send");
    cJSON_AddStringToObject(json, "protocol", "RAW");
    cJSON_AddStringToObject(json, "value", "9000,4500,560,560,560,1690");
    cJSON_AddNumberToObject(json, "bits", 6);
    cJSON_AddNumberToObject(json, "frequency", 38);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(1, mock_status.ir.send_raw_call_count);
    TEST_ASSERT_EQUAL(6, mock_status.ir.last_count);
    TEST_ASSERT_EQUAL(38000, mock_status.ir.last_frequency);
    
    TEST_ASSERT_EQUAL(9000, mock_status.ir.last_durations[0]);
    TEST_ASSERT_EQUAL(4500, mock_status.ir.last_durations[1]);
    TEST_ASSERT_EQUAL(560, mock_status.ir.last_durations[2]);
    TEST_ASSERT_EQUAL(560, mock_status.ir.last_durations[3]);
    TEST_ASSERT_EQUAL(560, mock_status.ir.last_durations[4]);
    TEST_ASSERT_EQUAL(1690, mock_status.ir.last_durations[5]);
    
    cJSON_Delete(json);
}

// 15. Test ir_send NEC protocol
void test_ir_send_nec(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "ir_send");
    cJSON_AddStringToObject(json, "protocol", "NEC");
    cJSON_AddStringToObject(json, "value", "A5"); // 0xA5 = 10100101 binary
    cJSON_AddNumberToObject(json, "bits", 8);
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(1, mock_status.ir.send_raw_call_count);
    TEST_ASSERT_EQUAL(38000, mock_status.ir.last_frequency);
    // NEC protocol length = Header(2) + bits * 2 + Footer(1) = 2 + 16 + 1 = 19
    TEST_ASSERT_EQUAL(19, mock_status.ir.last_count);
    
    // Check NEC header: 9000us high, 4500us low
    TEST_ASSERT_EQUAL(9000, mock_status.ir.last_durations[0]);
    TEST_ASSERT_EQUAL(4500, mock_status.ir.last_durations[1]);
    
    // bit 0 (1): 560us high, 1690us low
    TEST_ASSERT_EQUAL(560, mock_status.ir.last_durations[2]);
    TEST_ASSERT_EQUAL(1690, mock_status.ir.last_durations[3]);
    
    // bit 1 (0): 560us high, 560us low
    TEST_ASSERT_EQUAL(560, mock_status.ir.last_durations[4]);
    TEST_ASSERT_EQUAL(560, mock_status.ir.last_durations[5]);
    
    cJSON_Delete(json);
}

// 16. Test ir_send unsupported protocol
void test_ir_send_unsupported(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "ir_send");
    cJSON_AddStringToObject(json, "protocol", "RC5"); // Not supported
    cJSON_AddStringToObject(json, "value", "12");
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, err);
    
    cJSON_Delete(json);
}

// 17. Test ota_start success
void test_ota_start_success(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "ota_start");
    cJSON_AddStringToObject(json, "url", "https://example.com/firmware.bin");
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    TEST_ASSERT_EQUAL(1, mock_status.ota.start_call_count);
    TEST_ASSERT_EQUAL_STRING("https://example.com/firmware.bin", mock_status.ota.last_url);
    
    cJSON_Delete(json);
}

// 18. Test ota_start insecure url rejected
void test_ota_start_insecure_url(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "ota_start");
    cJSON_AddStringToObject(json, "url", "http://example.com/firmware.bin"); // Insecure HTTP
    
    esp_err_t err = command_dispatcher_execute(json);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    TEST_ASSERT_EQUAL(0, mock_status.ota.start_call_count);
    
    cJSON_Delete(json);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_dispatcher_null_json);
    RUN_TEST(test_dispatcher_missing_action);
    RUN_TEST(test_dispatcher_unknown_action);
    
    RUN_TEST(test_set_relay_success_on);
    RUN_TEST(test_set_relay_success_off);
    RUN_TEST(test_set_relay_invalid_pin);
    RUN_TEST(test_set_relay_missing_params);
    
    RUN_TEST(test_set_pwm_success);
    RUN_TEST(test_set_pwm_clamping_low);
    RUN_TEST(test_set_pwm_clamping_high);
    RUN_TEST(test_set_pwm_invalid_pin);
    
    RUN_TEST(test_control_ac_success_on);
    RUN_TEST(test_control_ac_power_off_cancels_timer);
    RUN_TEST(test_control_ac_temp_clamping_low);
    RUN_TEST(test_control_ac_temp_clamping_high);
    
    RUN_TEST(test_set_ac_timer_success);
    
    RUN_TEST(test_ir_send_raw);
    RUN_TEST(test_ir_send_nec);
    RUN_TEST(test_ir_send_unsupported);
    
    RUN_TEST(test_ota_start_success);
    RUN_TEST(test_ota_start_insecure_url);
    
    return UNITY_END();
}
