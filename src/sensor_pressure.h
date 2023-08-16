#pragma once

#include "stdbool.h"
#include "mgos.h"

#define PRESSURE_EVENT_BASE MGOS_EVENT_BASE('T', 'S', 'P')
enum pressure_event {
  PRESSURE_BASE = PRESSURE_EVENT_BASE,
  PRESSURE_MEASUREMENT,
  PRESSURE_FAIL
};

typedef struct pressure_status {
  int raw_adc;
} pressure_status_t;

bool sensor_pressure_init();