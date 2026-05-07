#include "math.h"
#include "stdlib.h"

#include "mgos.h"
#include "mgos_bme280.h"

#include "sensor_pressure.h"
#include "sensor_bme280.h"
#include "tank_volume.h"
#include "sensor.h"

static tank_volume_t tank_volume = {
    .tank_percentage = 0,
    .tank_liters = 0};

// tank is a cylinder lying on it's side width of 100cm and diameter 50cm
static const float tank_radius_cm         = 25.0;
static const float tank_length_cm         = 100.0;
static const float tank_radius_squared_cm2 = tank_radius_cm * tank_radius_cm;
static const float tank_maximum_liters    = 196.3;
static const float tank_liters_change_report_threshold = 1;

// Two-regime temperature compensation with hysteresis.
// Regime 0 (cold): applied when T < t_cold_thr
// Regime 1 (warm): applied when T > t_warm_thr
// comp_offset is adjusted on each regime switch so the compensated ADC
// output is continuous — no step jump in reported water level.
static float slope_cold  = 3.260f;
static float slope_warm  = 0.468f;
static float t_warm_thr  = 22.0f;
static float t_cold_thr  = 19.0f;
static float comp_offset = 0.0f;
static int   comp_regime = 0;

static double env_temperature = 0.0;

static void sync_comp_config(void) {
  mgos_sys_config_set_tank_temp_compensation_slope_cold(slope_cold);
  mgos_sys_config_set_tank_temp_compensation_slope_warm(slope_warm);
  mgos_sys_config_set_tank_temp_compensation_t_warm(t_warm_thr);
  mgos_sys_config_set_tank_temp_compensation_t_cold(t_cold_thr);
  mgos_sys_config_set_tank_temp_compensation_offset(comp_offset);
  mgos_sys_config_set_tank_temp_compensation_regime(comp_regime);
}

static void maybe_switch_regime(float T) {
  int new_regime = comp_regime;
  if (comp_regime == 0 && T > t_warm_thr) new_regime = 1;
  else if (comp_regime == 1 && T < t_cold_thr) new_regime = 0;
  if (new_regime == comp_regime) return;

  float old_slope = (comp_regime == 0) ? slope_cold : slope_warm;
  float new_slope = (new_regime  == 0) ? slope_cold : slope_warm;
  // Shift offset so output is unchanged at temperature T:
  // raw - old_slope*T + old_offset == raw - new_slope*T + new_offset
  // => new_offset = old_offset + (new_slope - old_slope) * T
  comp_offset += (new_slope - old_slope) * T;
  comp_regime  = new_regime;

  LOG(LL_INFO, ("Regime -> %s at %.1f°C  offset=%.2f",
                comp_regime ? "WARM" : "COLD", T, comp_offset));

  sync_comp_config();
  save_cfg(&mgos_sys_config, NULL);
}

void on_tank_water_height_change(observable_value_t *this)
{
  static float last_reported_liters = 0;
  float tank_water_height_cm = this->value.value;

  LOG(LL_INFO, ("Water height %f", tank_water_height_cm));

  float tank_volume_cm3 = tank_length_cm * (tank_radius_squared_cm2 * acos(1.0 - tank_water_height_cm / tank_radius_cm) - (tank_radius_cm - tank_water_height_cm) * sqrt(2.0 * tank_radius_cm * tank_water_height_cm - pow(tank_water_height_cm, 2)));
  tank_volume.tank_liters     = tank_volume_cm3 / 1000.0;
  tank_volume.tank_percentage = tank_volume.tank_liters / tank_maximum_liters * 100.0;

  if (tank_volume.tank_percentage < 100.0 &&
      fabs(tank_volume.tank_liters - last_reported_liters) < tank_liters_change_report_threshold) return;

  mgos_event_trigger(VOLUME_MEASUREMENT, &tank_volume);
  last_reported_liters = tank_volume.tank_liters;
}

static observable_value_t tank_water_height = {
    .value.value = 0,
    .name        = "tank_water_height",
    .filters     = ((void *)NULL),
    .observers   = ((void *)NULL),
    .set         = set_value,
    .process     = process_new_value,
    .notify      = notify_observers,
};

static filter_item_linear_fit_t pressure_percentage_fit = {
    .super.filter = filter_item_linear_fit_fn,
    .value_map    = {{0, 0}, {0, 0}},
    .slope_       = 0,
    .intercept_   = 0};

static filter_item_clamp_t clamp_percentage = {
    .super.filter = filter_item_clamp_fn,
    .min = 0,
    .max = 100};

filter_item_linear_fit_t percentage_water_height_fit = {
    .super.filter = filter_item_linear_fit_fn,
    .value_map    = {{0, 0}, {0, 0}},
    .slope_       = 0,
    .intercept_   = 0};

static void bme280_cb(int ev, void *evd, void *user_data UNUSED_ARG)
{
  if (ev != ENV_MEASUREMENT) return;
  struct mgos_bme280_data *env = evd;
  env_temperature = env->temp;
  maybe_switch_regime((float)env_temperature);
}

static void pressure_volume_cb(int ev, void *evd, void *user_data UNUSED_ARG)
{
  if (ev != PRESSURE_MEASUREMENT) return;
  pressure_status_t *pressure_status = evd;

  float current_slope  = (comp_regime == 0) ? slope_cold : slope_warm;
  float compensated_adc = pressure_status->raw_adc
                          - current_slope * (float)env_temperature
                          + comp_offset;

  LOG(LL_INFO, ("Raw adc %.2f  compensated %.2f  regime=%s  slope=%.4f  offset=%.2f  T=%.1f",
                pressure_status->raw_adc, compensated_adc,
                comp_regime ? "warm" : "cold",
                current_slope, comp_offset, (float)env_temperature));

  tank_water_height.process(&tank_water_height, compensated_adc);
}

void tank_volume_set_threshold(float pressure_low_threshold, float pressure_high_threshold)
{
  pressure_percentage_fit.value_map[0][0] = pressure_low_threshold;
  pressure_percentage_fit.value_map[0][1] = 0;
  pressure_percentage_fit.value_map[1][0] = pressure_high_threshold;
  pressure_percentage_fit.value_map[1][1] = 100;
  filter_linear_fit_calc(&pressure_percentage_fit);
}

// Sets both regime slopes to the same value — reverts to single-slope mode.
void tank_volume_set_slope(float slope) {
  tank_volume_set_coefficients(slope, slope, t_cold_thr, t_warm_thr);
}

// Updates regime slopes and thresholds.  Adjusts comp_offset immediately so
// the compensated output is continuous at the current temperature.
// Caller is responsible for persisting config (save_cfg after this call).
void tank_volume_set_coefficients(float sc, float sw, float tc, float tw) {
  float old_slope = (comp_regime == 0) ? slope_cold : slope_warm;
  float new_slope = (comp_regime == 0) ? sc : sw;
  comp_offset += (new_slope - old_slope) * (float)env_temperature;

  slope_cold = sc;
  slope_warm = sw;
  t_cold_thr = tc;
  t_warm_thr = tw;

  sync_comp_config();
}

void tank_volume_init(float pressure_low_threshold, float pressure_high_threshold)
{
  slope_cold  = mgos_sys_config_get_tank_temp_compensation_slope_cold();
  slope_warm  = mgos_sys_config_get_tank_temp_compensation_slope_warm();
  t_warm_thr  = mgos_sys_config_get_tank_temp_compensation_t_warm();
  t_cold_thr  = mgos_sys_config_get_tank_temp_compensation_t_cold();
  comp_offset = mgos_sys_config_get_tank_temp_compensation_offset();
  comp_regime = mgos_sys_config_get_tank_temp_compensation_regime();

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

  mgos_event_add_group_handler(ENV_EVENT_BASE,      bme280_cb,         NULL);
  mgos_event_add_group_handler(PRESSURE_EVENT_BASE, pressure_volume_cb, NULL);
}
