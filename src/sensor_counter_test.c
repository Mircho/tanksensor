// to test the counter you can use the ledc module
// to generate test frequency signal at the input pin

#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "mgos.h"
#include "mgos_rpc.h"
#include "sensor_counter_test.h"

#define TAG "Frequency counter tester"

#define SELF_FREQ 191

static gpio_num_t pulse_gpio_pin = GPIO_NUM_16;
#define CNT_TEST_LEDC_CHANNEL LEDC_CHANNEL_1

/**
 *  Used for internal frequency generation
 *  Configure LED PWM Controller
 *  to output sample pulses at freq Hz with 505 duty cycle
 */
static void ledc_init(uint16_t freq)
{
  // Prepare and then apply the LEDC PWM timer configuration
  ledc_timer_config_t ledc_timer;
  ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;
  ledc_timer.timer_num = LEDC_TIMER_1;
  ledc_timer.duty_resolution = LEDC_TIMER_10_BIT;
  ledc_timer.freq_hz = freq;
  ledc_timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&ledc_timer);

  // Prepare and then apply the LEDC PWM channel configuration
  ledc_channel_config_t ledc_channel;
  ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
  ledc_channel.channel = CNT_TEST_LEDC_CHANNEL;
  ledc_channel.timer_sel = LEDC_TIMER_1;
  ledc_channel.intr_type = LEDC_INTR_DISABLE;
  ledc_channel.gpio_num = pulse_gpio_pin;
  ledc_channel.duty = 512; // set duty at 50%
  ledc_channel.hpoint = 0;
  ledc_channel_config(&ledc_channel);

  ledc_timer_resume(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_1);
}

static void counter_test_handler(struct mg_rpc_request_info *ri, void *cb_arg UNUSED_ARG,
                                 struct mg_rpc_frame_info *fi UNUSED_ARG, struct mg_str args)
{
  int freq = -1;
  if (json_scanf(args.p, args.len,
                 ri->args_fmt,
                 &freq) < 1)
  {
    LOG(LL_INFO, ("%s, [Test counter] Error in request", TAG));
    mg_rpc_send_errorf(ri, 500, "Bad request. Expected {\"freq\":N}");
    return;
  }

  LOG(LL_DEBUG, ("%s, [Counter] frequency: %d", TAG, freq));

  if (freq == 0)
  {
    ledc_timer_pause(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_1);
    // gpio_set_pull_mode(pulse_gpio_pin, GPIO_PULLUP_ONLY);
    // gpio_set_direction(pulse_gpio_pin, GPIO_MODE_INPUT);
  }
  else
  {
    ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_1, freq);
    ledc_timer_resume(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_1);
  }
  mg_rpc_send_responsef(ri, "{status:%B,freq:%d}", true, freq);
}

bool sensor_counter_test_init()
{
  pulse_gpio_pin = GPIO_NUM_16;

  LOG(LL_INFO, ("%s, [Counter pin] pin %d", TAG, pulse_gpio_pin));

  ledc_init(SELF_FREQ);

  struct mg_rpc *c = mgos_rpc_get_global();
  mg_rpc_add_handler(c, "Counter.Test",
                     "{freq:%d}", counter_test_handler, NULL);
  return true;
}

bool sensor_counter_test_init_gpio_output() {
  gpio_matrix_out(pulse_gpio_pin, LEDC_HS_SIG_OUT0_IDX + CNT_TEST_LEDC_CHANNEL, false, false);
  return true; 
}