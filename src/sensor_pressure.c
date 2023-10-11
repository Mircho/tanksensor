#include "math.h"
#include "mgos_adc.h"
#include "mgos_system.h"
#include "mgos_timers.h"
#include "mgos_rpc.h"
#include "mgos_sys_config.h"
#include "mgos_config_util.h"

#include "sensor.h"
#include "sensor_pressure.h"

#define TAG "Pressure sensor"

static int pressure_adc_pin = -1;

static mgos_timer_id adc_timer_id = MGOS_INVALID_TIMER_ID;
static const int timer_period_ms = 40;

static pressure_status_t pressure_status = {
    .raw_adc = 0};

observable_value_t pressure_adc = {
    .value.value = 0,
    .name = "pressure_adc",
    .filters = ((void *)0),
    .observers = ((void *)0),
    .set = set_value,
    .process = process_new_value,
    .notify = notify_observers,
};

// moving average
filter_item_exp_moving_average_t pressure_ma_filter = {
    .super.filter = filter_item_exp_moving_average_fn,
    .initialized = false,
    .previous_value = 0,
    .alpha = 0.8,
    .pass_first = true
};

static void pressure_result_callback(observable_value_t *this)
{
  pressure_status.raw_adc = (int)this->value.value;
  mgos_event_trigger(PRESSURE_MEASUREMENT, &pressure_status);
}

static void pressure_measurement_callback(void *ud)
{
  int current_sample = mgos_adc_read(pressure_adc_pin);
  pressure_adc.process(&pressure_adc, current_sample);
}

bool pressure_sensor_stop()
{
  if (adc_timer_id != 0)
    mgos_clear_timer(adc_timer_id);
  return true;
}

bool sensor_pressure_init()
{
#ifndef MGOS_CONFIG_HAVE_BOARD_PRESSURE_PIN
  LOG(LL_INFO, ("%s, [Error] missing definition of pressure ADC pin in mos.yml", TAG));
  return false;
#endif
  pressure_adc_pin = mgos_sys_config_get_board_pressure_pin();
  assert(pressure_adc_pin > 0);

  LOG(LL_INFO, ("%s, [Pressure pin] pin %d", TAG, pressure_adc_pin));

  if (!mgos_adc_enable(pressure_adc_pin))
    return false;

  mgos_event_register_base(PRESSURE_EVENT_BASE, "Tank pressure events");

  add_filter(&pressure_adc, (filter_item_t *)&pressure_ma_filter);
  add_observer(&pressure_adc, pressure_result_callback);

  adc_timer_id = mgos_set_timer(timer_period_ms, MGOS_TIMER_REPEAT, pressure_measurement_callback, NULL);
  if (adc_timer_id == MGOS_INVALID_TIMER_ID)
    return false;

  pressure_status.raw_adc = mgos_adc_read(pressure_adc_pin);

  return true;
}