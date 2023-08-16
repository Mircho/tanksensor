#pragma once

#include "stdbool.h"
#include "driver/ledc.h"

#define CNT_TEST_LEDC_CHANNEL LEDC_CHANNEL_1

bool sensor_counter_test_init();
bool sensor_counter_test_init_gpio_output();