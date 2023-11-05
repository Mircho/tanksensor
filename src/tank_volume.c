#include "math.h"

#include "mgos.h"

#include "sensor_pressure.h"
#include "tank_volume.h"
#include "sensor.h"

static tank_volume_t tank_volume = {
    .tank_percentage = 0,
    .tank_liters = 0};

// tank is a cylinder lying on it's side width of 100cm and diameter 50cm
static const float tank_radius_cm = 25.0;
static const float tank_length_cm = 100.0;
static const float tank_radius_squared_cm2 = tank_radius_cm * tank_radius_cm;
static const float tank_maximum_liters = 197.0;
static const float tank_liters_change_report_threshold = 1.5;

void on_tank_water_height_change(observable_value_t *this)
{
  static float last_reported_liters = 0;
  float tank_water_height = this->value.value;

  LOG(LL_INFO, ("Water height %f", tank_water_height));

  float tank_volume_cm3 = tank_length_cm * (tank_radius_squared_cm2 * acos(1 - tank_water_height / tank_radius_cm) - (tank_radius_cm - tank_water_height) * sqrt(2 * tank_radius_cm * tank_water_height - pow(tank_water_height, 2)));
  tank_volume.tank_liters = tank_volume_cm3 / 1000.0;
  tank_volume.tank_percentage = tank_volume.tank_liters / tank_maximum_liters * 100.0;
  // decide if we need to report based on liters change
  if( fabs(tank_volume.tank_liters - last_reported_liters) > tank_liters_change_report_threshold ) {
    mgos_event_trigger(VOLUME_MEASUREMENT, &tank_volume);
    last_reported_liters = tank_volume.tank_liters;
  }
}

static observable_value_t tank_water_height = {
    .value.value = 0,
    .name = "tank_water_height",
    .filters = ((void *)NULL),
    .observers = ((void *)NULL),
    .set = set_value,
    .process = process_new_value,
    .notify = notify_observers,
};

// fit pressure to percentage
static filter_item_linear_fit_t pressure_percentage_fit = {
    .super.filter = filter_item_linear_fit_fn,
    .value_map = {{0, 0}, {0, 0}},
    .slope_ = 0,
    .intercept_ = 0};

static filter_item_clamp_t clamp_percentage = {
    .super.filter = filter_item_clamp_fn,
    .min = 0,
    .max = 100};

// fit percentage to water height
filter_item_linear_fit_t percentage_water_height_fit = {
    .super.filter = filter_item_linear_fit_fn,
    .value_map = {{0, 0}, {0, 0}},
    .slope_ = 0,
    .intercept_ = 0};

static void pressure_volume_cb(int ev, void *evd, void *user_data UNUSED_ARG)
{
  if(ev != PRESSURE_MEASUREMENT) return;
  pressure_status_t *pressure_status = evd;
  tank_water_height.process(&tank_water_height, pressure_status->raw_adc);
}

void tank_volume_set_threshold(float pressure_low_threshold, float pressure_high_threshold) 
{
  pressure_percentage_fit.value_map[0][0] = pressure_low_threshold;
  pressure_percentage_fit.value_map[0][1] = 0;
  pressure_percentage_fit.value_map[1][0] = pressure_high_threshold;
  pressure_percentage_fit.value_map[1][1] = 100;

  filter_linear_fit_calc(&pressure_percentage_fit);
}

void tank_volume_init(float pressure_low_threshold, float pressure_high_threshold)
{
  // init variables and filters
  tank_volume_set_threshold(pressure_low_threshold, pressure_high_threshold);

  percentage_water_height_fit.value_map[0][0] = 0;
  percentage_water_height_fit.value_map[0][1] = 0;
  percentage_water_height_fit.value_map[1][0] = 100;
  percentage_water_height_fit.value_map[1][1] = 2.0 * tank_radius_cm;

  filter_linear_fit_calc(&percentage_water_height_fit);

  add_filter(&tank_water_height, (filter_item_t *)&pressure_percentage_fit);
  add_filter(&tank_water_height, (filter_item_t *)&clamp_percentage);
  add_filter(&tank_water_height, (filter_item_t *)&percentage_water_height_fit);

  add_observer(&tank_water_height, on_tank_water_height_change);

  mgos_event_add_group_handler(PRESSURE_EVENT_BASE, pressure_volume_cb, NULL);
}