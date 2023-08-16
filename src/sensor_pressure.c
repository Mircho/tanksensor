#include "math.h"
#include "common/json_utils.h"
#include "frozen.h"
#include "mgos_adc.h"
#include "mgos_system.h"
#include "mgos_timers.h"
#include "mgos_rpc.h"
#include "mgos_sys_config.h"
#include "mgos_config_util.h"
#include "sensor_pressure.h"

#define TAG "Pressure sensor"

static const int timer_period_msec = 40;
static const uint8_t adc_samples_count = 55;
static mgos_timer_id adc_timer_id = MGOS_INVALID_TIMER_ID;

static int pressure_adc_pin = -1;

static pressure_status_t pressure_status = {
    .raw_adc = 0};

static void pressure_measurement_callback(void *ud)
{
  static uint8_t samples_counter = adc_samples_count;
  static int oversampled_adc = 0;
  oversampled_adc += mgos_adc_read(pressure_adc_pin);
  samples_counter--;
  if(samples_counter == 0) {
    pressure_status.raw_adc = (int)round(oversampled_adc/adc_samples_count);
    mgos_event_trigger(PRESSURE_MEASUREMENT, &pressure_status);
    samples_counter = adc_samples_count;
    oversampled_adc = 0;
  }
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

  LOG(LL_INFO, ("%s, [Error] missing definitiion of pressure ADC pin in mos.yml", TAG));
  return false;

  #endif

  pressure_adc_pin = mgos_sys_config_get_board_pressure_pin();
  assert(pressure_adc_pin>0);

  if (!mgos_adc_enable(pressure_adc_pin))
    return false;

  mgos_event_register_base(PRESSURE_EVENT_BASE, "Tank pressure events");

  adc_timer_id = mgos_set_timer(timer_period_msec, MGOS_TIMER_REPEAT, pressure_measurement_callback, NULL);
  if (adc_timer_id == MGOS_INVALID_TIMER_ID)
    return false;

  return true;
}