#include "math.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"

#include "mgos_system.h"
#include "mgos_freertos.h"
#include "mgos_rpc.h"
#include "mgos_sys_config.h"
#include "mgos_config_util.h"

#include "mgos_ota.h"

#include "sensor.h"
#include "sensor_pressure.h"

#define TAG "Pressure sensor"

// DMA continuous sampling: the ADC digital controller (fed through I2S0 on
// the classic ESP32, so I2S0 must stay unused elsewhere) free-runs at
// adc_sample_freq_hz. A reader task drains the DMA buffers, averages blocks
// of adc_block_samples and hands one block mean per 100ms to the filter
// pipeline on the mongoose task.
static const uint32_t adc_sample_freq_hz = 20000; // hardware minimum for the digi controller
static const size_t adc_block_samples = 2000;     // 100 ms per block mean
static const size_t blocks_per_result = 10;       // one published result per second

#define ADC_RESULT_BYTES 2 // TYPE1 frame: 12-bit data + 4-bit channel
#define ADC_READ_CHUNK_BYTES 256

static int pressure_adc_pin = -1;
static int pressure_adc_channel = -1;

static TaskHandle_t adc_task_handle = NULL;
static volatile bool adc_task_run = false;

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

// median over block means (rejects blocks skewed by WiFi bursts, window=5)
filter_item_median_t pressure_median_filter = {
    .super.filter = filter_item_median_fn,
    .window_size = 5,
    .count_ = 0,
    .head_ = 0,
    .buf_ = {0}
};

// average of block means -> one result per second
filter_item_average_t pressure_avg_filter = {
    .super.filter = filter_item_average_fn,
    .number_of_samples = blocks_per_result,
    .sample_counter_ = blocks_per_result,
    .accumulator_ = 0,
    .pass_first = false
};

// moving average
filter_item_exp_moving_average_t pressure_ma_filter = {
    .super.filter = filter_item_exp_moving_average_fn,
    .initialized = false,
    .previous_value = 0,
    .alpha = 0.1,
    .pass_first = false
};

static int adc1_pin_to_channel(int pin)
{
  switch (pin)
  {
    case 36: return ADC1_CHANNEL_0;
    case 37: return ADC1_CHANNEL_1;
    case 38: return ADC1_CHANNEL_2;
    case 39: return ADC1_CHANNEL_3;
    case 32: return ADC1_CHANNEL_4;
    case 33: return ADC1_CHANNEL_5;
    case 34: return ADC1_CHANNEL_6;
    case 35: return ADC1_CHANNEL_7;
  }
  return -1;
}

static void pressure_result_callback(observable_value_t *this)
{
  LOG(LL_INFO, ("%s, Pressure result %.2f", TAG, this->value.value));
  pressure_status.raw_adc = this->value.value;
  mgos_event_trigger(PRESSURE_MEASUREMENT, &pressure_status);
}

// runs on the mongoose task; block mean arrives as milli-counts packed in arg
static void pressure_block_mean_callback(void *arg)
{
  double block_mean = (double)(uint32_t)(uintptr_t)arg / 1000.0;
  LOG(LL_DEBUG, ("%s, Pressure adc block mean %.3f", TAG, block_mean));
  pressure_adc.process(&pressure_adc, block_mean);
}

static void pressure_adc_task(void *arg)
{
  uint8_t buf[ADC_READ_CHUNK_BYTES];
  uint64_t accumulator = 0;
  size_t sample_count = 0;

  while (adc_task_run)
  {
    uint32_t read_len = 0;
    adc_digi_read_bytes(buf, sizeof(buf), &read_len, 1000 /* ms */);

    for (uint32_t i = 0; i + ADC_RESULT_BYTES <= read_len; i += ADC_RESULT_BYTES)
    {
      adc_digi_output_data_t *p = (adc_digi_output_data_t *)&buf[i];
      if (p->type1.channel != pressure_adc_channel)
        continue;
      accumulator += p->type1.data;
      if (++sample_count == adc_block_samples)
      {
        uint32_t mean_milli = (uint32_t)((accumulator * 1000) / adc_block_samples);
        mgos_invoke_cb(pressure_block_mean_callback, (void *)(uintptr_t)mean_milli, 0);
        accumulator = 0;
        sample_count = 0;
      }
    }
  }

  // driver teardown happens in pressure_sampler_stop_sync on the main task —
  // the task only signals its exit here
  adc_task_handle = NULL;
  vTaskDelete(NULL);
}

// Fully stops the sampler before returning: waits for the reader task to
// exit, then tears down the digi driver from the calling (main) task — the
// same task/core that allocated the interrupt. Must complete before any
// flash writes start: a live DMA stream during a flash erase crashes the
// chip.
static void pressure_sampler_stop_sync()
{
  if (adc_task_handle == NULL) return;
  adc_task_run = false;
  // reader task notices the flag within one adc_digi_read_bytes timeout
  for (int i = 0; i < 200 && adc_task_handle != NULL; i++)
    mgos_msleep(10);
  adc_digi_stop();
  adc_digi_deinitialize();
}

bool pressure_sensor_stop()
{
  pressure_sampler_stop_sync();
  return true;
}

static bool pressure_sampler_start()
{
  if (adc_task_handle != NULL) return true;

  adc_digi_init_config_t adc_dma_config = {
      .max_store_buf_size = 4096,
      .conv_num_each_intr = ADC_READ_CHUNK_BYTES,
      .adc1_chan_mask = BIT(pressure_adc_channel),
      .adc2_chan_mask = 0,
  };
  if (adc_digi_initialize(&adc_dma_config) != ESP_OK)
  {
    LOG(LL_INFO, ("%s, [Error] adc_digi_initialize failed", TAG));
    return false;
  }

  adc_digi_pattern_config_t adc_pattern = {
      .atten = ADC_ATTEN_DB_11,
      .channel = pressure_adc_channel,
      .unit = 0, // ADC1
      .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
  };
  adc_digi_configuration_t dig_cfg = {
      .conv_limit_en = 1, // must be enabled on the classic ESP32
      .conv_limit_num = 250,
      .pattern_num = 1,
      .adc_pattern = &adc_pattern,
      .sample_freq_hz = adc_sample_freq_hz,
      .conv_mode = ADC_CONV_SINGLE_UNIT_1,
      .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
  };
  if (adc_digi_controller_configure(&dig_cfg) != ESP_OK)
  {
    LOG(LL_INFO, ("%s, [Error] adc_digi_controller_configure failed", TAG));
    adc_digi_deinitialize();
    return false;
  }

  if (adc_digi_start() != ESP_OK)
  {
    LOG(LL_INFO, ("%s, [Error] adc_digi_start failed", TAG));
    adc_digi_deinitialize();
    return false;
  }

  adc_task_run = true;
  if (xTaskCreate(pressure_adc_task, "pressure_adc", 4096, NULL, MGOS_TASK_PRIORITY - 1, &adc_task_handle) != pdPASS)
  {
    adc_task_run = false;
    adc_digi_stop();
    adc_digi_deinitialize();
    return false;
  }

  return true;
}

// flash writes during an update contend with the continuous DMA stream —
// pause the sampler for the duration, resume only if the update fails
// (on success the device reboots)
static void ota_cb(int ev, void *evd, void *user_data)
{
  if (ev == MGOS_EVENT_OTA_BEGIN)
  {
    LOG(LL_INFO, ("%s, OTA begin - stopping ADC sampler", TAG));
    pressure_sampler_stop_sync();
    LOG(LL_INFO, ("%s, ADC sampler stopped, OTA may proceed", TAG));
  }
  else if (ev == MGOS_EVENT_OTA_STATUS)
  {
    struct mgos_ota_status *status = evd;
    if (status->state == MGOS_OTA_STATE_ERROR && adc_task_handle == NULL)
    {
      LOG(LL_INFO, ("%s, OTA failed - resuming ADC sampler", TAG));
      pressure_sampler_start();
    }
  }
  (void)user_data;
}

bool sensor_pressure_init()
{
#ifndef MGOS_CONFIG_HAVE_BOARD_PRESSURE_PIN
  LOG(LL_INFO, ("%s, [Error] missing definition of pressure ADC pin in mos.yml", TAG));
  return false;
#endif
  pressure_adc_pin = mgos_sys_config_get_board_pressure_pin();
  assert(pressure_adc_pin > 0);

  pressure_adc_channel = adc1_pin_to_channel(pressure_adc_pin);
  if (pressure_adc_channel < 0)
  {
    LOG(LL_INFO, ("%s, [Error] pin %d is not an ADC1 pin, DMA mode needs ADC1", TAG, pressure_adc_pin));
    return false;
  }

  LOG(LL_INFO, ("%s, [Pressure pin] pin %d, ADC1 channel %d, %lu Hz DMA sampling", TAG,
                pressure_adc_pin, pressure_adc_channel, (unsigned long)adc_sample_freq_hz));

  mgos_event_register_base(PRESSURE_EVENT_BASE, "Tank pressure events");

  add_filter(&pressure_adc, (filter_item_t *)&pressure_median_filter);
  add_filter(&pressure_adc, (filter_item_t *)&pressure_avg_filter);
  add_filter(&pressure_adc, (filter_item_t *)&pressure_ma_filter);
  add_observer(&pressure_adc, pressure_result_callback);

  mgos_event_add_group_handler(MGOS_EVENT_OTA_BASE, ota_cb, NULL);

  return pressure_sampler_start();
}
