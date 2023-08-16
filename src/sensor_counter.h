#pragma once

#include "stdbool.h"
#include "mgos.h"

#define COUNTER_EVENT_BASE MGOS_EVENT_BASE('T', 'O', 'C')
enum tank_overflow_event {
  COUNTER_BASE = COUNTER_EVENT_BASE,
  COUNTER_CHANGE
};

typedef struct gpio_counter {
  uint16_t count;
  uint16_t frequency;
} gpio_counter_t;

bool sensor_counter_init();
bool sensor_counter_start();
bool sensor_counter_stop();
