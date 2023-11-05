/**
 * Reference:
 * https://docs.espressif.com/projects/esp-idf/en/v4.3.5/esp32/api-reference/peripherals/pcnt.html
 * https://docs.espressif.com/projects/esp-idf/en/v4.3.5/esp32/api-reference/peripherals/rmt.html
 * https://github.com/DavidAntliff/esp32-freqcount
 *  https://esp32.com/viewtopic.php?t=4953&start=10
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/pcnt.h"
#include "driver/rmt.h"

#include "sensor_counter.h"

//can be disabled in mos.yml by setting the var to != 1
#if FREQUENCY_TEST_MODE==1
#include "sensor_counter_test.h"
#endif

#define TAG "Frequency counter"

static gpio_counter_t gpio_counter = {
    .count = 0,
    .frequency = 0};

// use this for the intermediary readings
// to compare against last reported
static gpio_counter_t gpio_counter_reading = {
    .count = 0,
    .frequency = 0};

#define RMT_MEM_BLOCK_BYTE_NUM RMT_MEM_ITEM_NUM

// we rely on constant 80MHz frequency
static const double apb_clock_frequency_hz = 80000000.0;
// define the RMT sacrificial pin here, don't expose it in config
static const gpio_num_t rmt_gpio_pin = GPIO_NUM_17;
// RMT is not used elsewhere, grab first channel
static const rmt_channel_t rmt_channel = RMT_CHANNEL_0;
// RMT clock divider.
// APB clock is used (80MHz), at 160 this gives 2uS time resolution
static const uint8_t rmt_clk_div = 160;
// period in seconds of one rmt tick == 1/(apb_clock/rmt_clk_div)
static const double rmt_period_sec = (double)(rmt_clk_div) / apb_clock_frequency_hz;
// rmt_item is a 2 byte word, highest bit used to encode output state
static const uint16_t max_rmt_ticks_per_item = 0xFFFF & ~(1 << 15);
// maximum rmt blocks to be used
#define MAX_RMT_BLOCKS 1

#define RMT_ITEMS_BYTES (RMT_MEM_BLOCK_BYTE_NUM * MAX_RMT_BLOCKS)
#define RMT_BLOCK_SIZE 64
#define RMT_ITEMS_NUM (RMT_BLOCK_SIZE * MAX_RMT_BLOCKS)
static rmt_item32_t rmt_items[RMT_ITEMS_NUM] = {0};

// interval for repeated input scan
static const float sampling_window_sec = 1;
// how much to load in RMT
static const float sampling_period_sec = 1.1;

// static const gpio_num_t pulse_gpio_pin = GPIO_NUM_34;
static gpio_num_t pulse_gpio_pin = GPIO_NUM_16;
static const pcnt_unit_t pcnt_unit = PCNT_UNIT_0;
static const pcnt_channel_t pcnt_channel = PCNT_CHANNEL_0;

// depending on connected device can be
// GPIO_FLOATING, GPIO_PULLUP_ONLY etc.
#define PCNT_GPIO_PULL_MODE GPIO_PULLUP_ONLY
// GPIO_MODE_INPUT, GPIO_MODE_INPUT_OUTPUT
#define PCNT_DIRECTION GPIO_MODE_INPUT
// at 80MHz APB clock this means that maximum frequency would be
// about 39100 Hz, we count both level swings and divide by 2
// depending on pulse source this migth need to be reduced
static const uint16_t pcnt_filter_length = 1023;

// task
static TaskHandle_t frequency_task_handle;
static const int task_delay_ticks = sampling_period_sec * 1000 / portTICK_RATE_MS;
static const uint32_t terminate_task = 0x01;

void clear_task_handle_on_exit(void *arg UNUSED_ARG)
{
  LOG(LL_DEBUG, ("%s, [FREQUENCY TASK] clear task handle", TAG));
  frequency_task_handle = NULL;
}

// emit the counter readings
void process_counter_update(void *ard UNUSED_ARG)
{
  gpio_counter = gpio_counter_reading;
  mgos_event_trigger(COUNTER_CHANGE, &gpio_counter);
}

int frequency_count_init(void)
{
  // using one rmt block at this clock divider
  // allows for a window of max 10 sec
  rmt_config_t rmt_tx = {
    .rmt_mode = RMT_MODE_TX,
    .channel = rmt_channel,
    .gpio_num = rmt_gpio_pin,
    .clk_div = rmt_clk_div,
    .mem_block_num = MAX_RMT_BLOCKS,
    .flags = 0,
    .tx_config = {
      .loop_en = false,
      .carrier_en = false,
      .idle_output_en = true,
      .idle_level = RMT_IDLE_LEVEL_LOW
    }
  };
  ESP_ERROR_CHECK(rmt_config(&rmt_tx));
  ESP_ERROR_CHECK(rmt_driver_install(rmt_tx.channel, 0, 0));

  LOG(LL_INFO, ("%s, [FREQUENCY TASK] RMT Init", TAG));

  pcnt_config_t pcnt_config = {
    .unit = pcnt_unit,
    //.unit = pcnt_unit,
    .channel = pcnt_channel,
    // input pin
    .pulse_gpio_num = pulse_gpio_pin,
    // control gate pin
    .ctrl_gpio_num = rmt_gpio_pin,
    .hctrl_mode = PCNT_MODE_KEEP,
    .lctrl_mode = PCNT_MODE_DISABLE,
    // count both rising and falling edges
    .pos_mode = PCNT_COUNT_INC,
    .neg_mode = PCNT_COUNT_INC,
    .counter_h_lim = 1000,
    .counter_l_lim = -1000,
  };
  ESP_ERROR_CHECK(pcnt_unit_config(&pcnt_config));

  // enable counter filter - at 80MHz APB CLK, 1000 pulses is max 80,000 Hz, so ignore pulses less than 12.5 us.
  pcnt_set_filter_value(pcnt_unit, pcnt_filter_length);
  pcnt_filter_enable(pcnt_unit);

  gpio_set_pull_mode(pulse_gpio_pin, PCNT_GPIO_PULL_MODE);
  gpio_set_direction(pulse_gpio_pin, PCNT_DIRECTION);

  #if FREQUENCY_TEST_MODE==1
    sensor_counter_test_init_gpio_output();
  #endif

  *rmt_items = (rmt_item32_t){0};

  uint32_t duration_rmt_ticks = sampling_window_sec / rmt_period_sec;
  uint16_t num_rmt_items = 0;

  while (duration_rmt_ticks > 0)
  {
    uint32_t item_duration_rmt_ticks = (duration_rmt_ticks > max_rmt_ticks_per_item) ? max_rmt_ticks_per_item : duration_rmt_ticks;
    rmt_items[num_rmt_items].level0 = 1;
    rmt_items[num_rmt_items].duration0 = item_duration_rmt_ticks;
    duration_rmt_ticks -= item_duration_rmt_ticks;

    if (duration_rmt_ticks > 0)
    {
      item_duration_rmt_ticks = (duration_rmt_ticks > max_rmt_ticks_per_item) ? max_rmt_ticks_per_item : duration_rmt_ticks;
      rmt_items[num_rmt_items].level1 = 1;
      rmt_items[num_rmt_items].duration1 = item_duration_rmt_ticks;
      duration_rmt_ticks -= item_duration_rmt_ticks;
    }
    else
    {
      rmt_items[num_rmt_items].level1 = 0;
      rmt_items[num_rmt_items].duration1 = 0;
    }
    ++num_rmt_items;
  }

  LOG(LL_INFO, ("%s, [FREQUENCY TASK] Number of RMT items %d, max items %d", TAG, num_rmt_items, RMT_ITEMS_BYTES / sizeof(rmt_item32_t)));

  return num_rmt_items;
}

void frequency_count_teardown(void)
{
  ESP_ERROR_CHECK(rmt_driver_uninstall(rmt_channel));
}

void frequency_count_task_function(void *pvParameter)
{
  int16_t pin_change_count;

  int num_rmt_items = frequency_count_init();
  TickType_t last_wake_time_ticks = xTaskGetTickCount();

  // this only inits the gpio matrix output
  // sensor_counter_test_start();

  while (true)
  {
    double frequency_hz;
    // clear counter
    pcnt_counter_pause(pcnt_unit);
    pcnt_counter_clear(pcnt_unit);
    pcnt_counter_resume(pcnt_unit);
    // start sampling window
    rmt_write_items(rmt_channel, rmt_items, num_rmt_items, false);
    // esp_err_t rmt_tx_res = rmt_wait_tx_done(rmt_channel, portMAX_DELAY);
    rmt_wait_tx_done(rmt_channel, portMAX_DELAY);

    // read counter
    pcnt_get_counter_value(pcnt_unit, &pin_change_count);

    LOG(LL_DEBUG, ("%s, [FREQUENCY TASK] pcnt counter %d", TAG, pin_change_count));

    // we count both gpio swings thus we divide by 2
    // and then calculate Hz
    frequency_hz = pin_change_count / 2.0 / sampling_window_sec;
    LOG(LL_DEBUG, ("%s, [FREQUENCY TASK] frequency %f", TAG, frequency_hz));

    gpio_counter_reading.count = pin_change_count;
    gpio_counter_reading.frequency = frequency_hz;

    mgos_invoke_cb(process_counter_update, NULL, false);

    if (ulTaskNotifyTake(pdFALSE, 0) == terminate_task)
      break;
    if (task_delay_ticks > 0)
    {
      vTaskDelayUntil(&last_wake_time_ticks, task_delay_ticks);
    }
  }

  LOG(LL_INFO, ("%s, [FREQUENCY TASK] stop task", TAG));

  frequency_count_teardown();

  pcnt_counter_pause(pcnt_unit);
  pcnt_counter_clear(pcnt_unit);

  gpio_counter_reading.count = 0;
  gpio_counter_reading.frequency = 0;

  mgos_invoke_cb(process_counter_update, NULL, false);

  mgos_invoke_cb(clear_task_handle_on_exit, NULL, false);

  vTaskDelete(NULL);
}

bool sensor_counter_start()
{
  if (frequency_task_handle != NULL)
    return false;
  BaseType_t task_create_result = xTaskCreate(frequency_count_task_function, "frequency_count", 4096, NULL, 5, &frequency_task_handle);

  if (task_create_result == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY)
    return false;

  return true;
}

bool sensor_counter_stop()
{
  if (frequency_task_handle == NULL)
    return false;
  xTaskNotify(frequency_task_handle, terminate_task, eSetValueWithOverwrite);
  return true;
}

bool sensor_counter_init()
{
#ifndef MGOS_CONFIG_HAVE_BOARD_FREQUENCY_PIN
  LOG(LL_INFO, ("%s, [Error] missing definition of counter pin in mos.yml", TAG));
  return false;
#endif

  pulse_gpio_pin = mgos_sys_config_get_board_frequency_pin();
  assert(pulse_gpio_pin > 0);

  LOG(LL_INFO, ("%s, [Counter pin] pin %d", TAG, pulse_gpio_pin));

#if FREQUENCY_TEST_MODE==1
  sensor_counter_test_init();
#endif

  return sensor_counter_start();
}