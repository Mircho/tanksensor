// #include "mgos_system.h"
#include "mgos_timers.h"
#include "sensor_bme280.h"

#define TAG "BME280 sensor"

static const int timer_period_msec = 2500;
static mgos_timer_id bme280_timer_id = MGOS_INVALID_TIMER_ID;
static const uint8_t bme280_i2c_addr = 0x76;
static struct mgos_bme280 *bme280_sensor;

static struct mgos_bme280_data environment_status = {
    .humid = 0,
    .press = 0,
    .temp = 0};

static void bme280_measurement_callback(void *ud)
{
  mgos_bme280_read(bme280_sensor, &environment_status);
  mgos_event_trigger(ENV_MEASUREMENT, &environment_status);
}

bool sensor_bme280_init()
{
  mgos_event_register_base(ENV_EVENT_BASE, "BME280 events");

  bme280_sensor = mgos_bme280_i2c_create(bme280_i2c_addr);

  if(bme280_sensor == NULL) return false;

  bme280_timer_id = mgos_set_timer(timer_period_msec, MGOS_TIMER_REPEAT, bme280_measurement_callback, NULL);
  if (bme280_timer_id == MGOS_INVALID_TIMER_ID)
    return false;

  return true;
}