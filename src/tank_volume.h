#pragma once

#define VOLUME_EVENT_BASE MGOS_EVENT_BASE('T', 'V', 'L')
enum volume_event {
  VOLUME_BASE = VOLUME_EVENT_BASE,
  VOLUME_MEASUREMENT,
  VOLUME_FAIL
};

typedef struct tank_volume {
  float tank_percentage;
  float tank_liters;
} tank_volume_t;


void tank_volume_init(float pressure_low_threshold, float pressure_high_threshold);