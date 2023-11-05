#pragma once

#include "stdbool.h"
#include "mgos.h"

#define ENV_EVENT_BASE MGOS_EVENT_BASE('B', 'M', 'E')
enum environment_sensor_event {
  ENV_BASE = ENV_EVENT_BASE,
  ENV_MEASUREMENT,
};

bool sensor_bme280_init();