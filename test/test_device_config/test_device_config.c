#include <unity.h>
#include "device_config.h"
#include "../mocks/mocks.c"

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_gpio_pin_to_endpoint(void) {
    TEST_ASSERT_EQUAL_INT(1, gpio_pin_to_endpoint(RELAY_1_PIN));
    TEST_ASSERT_EQUAL_INT(2, gpio_pin_to_endpoint(RELAY_2_PIN));
    TEST_ASSERT_EQUAL_INT(3, gpio_pin_to_endpoint(RELAY_3_PIN));
    TEST_ASSERT_EQUAL_INT(4, gpio_pin_to_endpoint(RELAY_4_PIN));
    TEST_ASSERT_EQUAL_INT(0, gpio_pin_to_endpoint(999)); // Unknown pin
}

void test_pwm_pin_to_endpoint(void) {
    TEST_ASSERT_EQUAL_INT(5, pwm_pin_to_endpoint(PWM_LAMP_PIN));
    TEST_ASSERT_EQUAL_INT(6, pwm_pin_to_endpoint(PWM_RGB_R_PIN));
    TEST_ASSERT_EQUAL_INT(6, pwm_pin_to_endpoint(PWM_RGB_G_PIN));
    TEST_ASSERT_EQUAL_INT(6, pwm_pin_to_endpoint(PWM_RGB_B_PIN));
    TEST_ASSERT_EQUAL_INT(0, pwm_pin_to_endpoint(999)); // Unknown pin
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_gpio_pin_to_endpoint);
    RUN_TEST(test_pwm_pin_to_endpoint);
    return UNITY_END();
}
