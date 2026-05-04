#pragma once

#include "stdbool.h"
#include "mgos.h"

#define CALIBRATION_EVENT_BASE MGOS_EVENT_BASE('C', 'A', 'L')
enum calibration_event {
  CALIBRATION_BASE = CALIBRATION_EVENT_BASE,
  CALIBRATION_UPDATED,
};

typedef struct calibration_status {
  float slope;
  float r2;
  int   n_samples;
  float temp_min;
  float temp_max;
  bool  ready;
} calibration_status_t;

void sensor_calibration_init(void);
const calibration_status_t *sensor_calibration_get_status(void);
