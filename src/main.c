/**
 * Water tank controller firmware
 * BMP280
 * Analog pressure sensor at the tank output
 * Pulse counter for the overflow
 */
#include "math.h"

#include "mgos.h"
#include "mongoose.h"
#include "mgos_timers.h"
#include "mgos_http_server.h"
#include "mgos_rpc.h"
#include "mgos_mqtt.h"
#include "mgos_neopixel.h"

#include "sensor_bme280.h"
#include "sensor_pressure.h"
#include "sensor_counter.h"
#include "tank_volume.h"
//#include "sensor.h"

#define TAG "Tank sensor main unit"

enum WS_ENDPOINTS
{
  WS_ENDPOINT_START = 1000,
  WS_ENDPOINT_STATUS,
  WS_ENDPOINT_RAW
};

// notify timer
static const int notify_timer_period_msec = 5000;
static mgos_timer_id notify_timer_id = MGOS_INVALID_TIMER_ID;

// tank volume
static const float tank_maximum_liters = 197.0;
// threshold values for reporting full or empty status
static float liters_low_value = -1;
static float liters_high_value = -1;

// pressure
static int pressure_low_value = -1;
static int pressure_high_value = -1;
// counter threshold
static int freq_thr_hz = -1;
// maximum frequency threshold
static const int max_freq_thr_hz = 200;

static const char *JSON_HEADERS = "Connection: close\r\nContent-Type: application/json";
static const char *pressure_limits_fmt = "{low_thr:%i, high_thr:%i}";
static const char *tank_limits_fmt = "{low_thr:%f, high_thr:%f}";
static const char *freq_thr_fmt = "{freq_thr:%i}";

const uint8_t RGB_PIN = 5;

struct mgos_neopixel *board_rgb = NULL;

typedef enum notify_type
{
  NOTIFY_TIMER = 0,
  NOTIFY_RAW,
  NOTIFY_STATUS
} notify_type_t;

typedef enum tank_status
{
  TANK_LOW = 0,
  TANK_NORMAL,
  TANK_FULL,
} tank_status_t;

char *status_text[] = {
    [TANK_LOW] = "low",
    [TANK_NORMAL] = "normal",
    [TANK_FULL] = "full"};

struct sensor_info
{
  time_t timestamp;
  double air_temperature;
  double air_pressure;
  double air_humidity;
  tank_status_t tank_status;
  bool tank_overflow;
  float tank_liters;
  float tank_percentage;
} sensor_info = {
    .timestamp = 0,
    .air_temperature = 0.0,
    .air_pressure = 0.0,
    .air_humidity = 0.0,
    .tank_status = TANK_LOW,
    .tank_overflow = false,
    .tank_liters = 0.0,
    .tank_percentage = 0.0
};

struct sensor_raw
{
  time_t    timestamp;
  uint16_t  tank_pressure_adc;
  uint16_t  counter_count;
  float     counter_frequency;
} sensor_raw = {
  .timestamp          = 0,
  .tank_pressure_adc  = 0,
  .counter_count      = 0,
  .counter_frequency  = 0
};

static void notify_listeners(notify_type_t notify_reason);

// deferred cleanup
void cleanup_post_data(char **buffer) {
  if(*buffer != NULL) free(*buffer);
}

void cleanup_mbuf(struct mbuf *buffer) {
  if(buffer != NULL) mbuf_free(buffer);
}

// caller has to dispose of memory
const struct mbuf *getSatusAsJSON(struct mbuf *buffer)
{
  struct json_out json_result = JSON_OUT_MBUF(buffer);
  // mbuf_init(buffer, 1024);
  json_printf(&json_result,
              "{"
              "timestamp: %d,"
              "air_temperature: %4.2f,"
              "air_pressure: %5.1f,"
              "air_humidity: %4.1f,"
              "tank_liters: %4.1f,"
              "tank_percentage: %3.1f,"
              "tank_status: \"%s\","
              "tank_overflow: %B"
              "}",
              sensor_info.timestamp,
              sensor_info.air_temperature,
              sensor_info.air_pressure,
              sensor_info.air_humidity,
              sensor_info.tank_liters,
              sensor_info.tank_percentage,
              status_text[sensor_info.tank_status],
              sensor_info.tank_overflow);
  return buffer;
}

const struct mbuf *getRawAsJSON(struct mbuf *buffer)
{
  struct json_out json_result = JSON_OUT_MBUF(buffer);
  // mbuf_init(buffer, 1024);
  json_printf(  &json_result,
                "{"
                "timestamp: %d,"
                "tank_pressure_adc: %d,"
                "tank_overflow_count: %d,"
                "tank_overflow_frequency: %3.1f"
                "}",
                sensor_raw.timestamp,
                sensor_raw.tank_pressure_adc,
                sensor_raw.counter_count,
                sensor_raw.counter_frequency
                );
  return buffer;
}

static void rpc_status_handler(struct mg_rpc_request_info *ri, void *cb_arg UNUSED_ARG,
                               struct mg_rpc_frame_info *fi UNUSED_ARG, struct mg_str args)
{
  LOG(LL_DEBUG, ("RPC: Status requested"));
  struct mbuf response_buffer __attribute__((__cleanup__(cleanup_mbuf)));
  mbuf_init(&response_buffer, 1024);
  getSatusAsJSON(&response_buffer);

  if (response_buffer.len > 0)
  {
    mg_rpc_send_responsef(ri, "%.*s", response_buffer.len, response_buffer.buf);
  }
  else
  {
    mg_rpc_send_errorf(ri, 500, "Failed to get status");
  }
  notify_listeners(NOTIFY_STATUS);
}

static void http_handler(struct mg_connection *c, int ev, void *p, void *user_data)
{
  struct http_message *hm = (struct http_message *)p;
  // carry over the flag from the endpoint to the connection
  if (ev == MG_EV_WEBSOCKET_HANDSHAKE_DONE)
  {
    c->user_data = user_data;
    return;
  }
  if (ev != MG_EV_HTTP_REQUEST)
    return;
  LOG(LL_DEBUG, ("HTTP: Status requested"));

  struct mbuf response_buffer __attribute__((__cleanup__(cleanup_mbuf)));
  mbuf_init(&response_buffer, 1024);

  if(strncmp(mgos_sys_config_get_http_status_url(), hm->uri.p, hm->uri.len) == 0) {
    getSatusAsJSON(&response_buffer);
  }
  if(strncmp(mgos_sys_config_get_http_raw_url(), hm->uri.p, hm->uri.len) == 0) {
    getRawAsJSON(&response_buffer);
  }

  if (response_buffer.len > 0)
  {
    mg_send_head(c, 200, response_buffer.len, JSON_HEADERS);
    mg_send(c, response_buffer.buf, response_buffer.len);
  }
  else
  {
    mg_send_head(c, 500, 0, JSON_HEADERS);
    mg_send(c, "", 0);
  }
  c->flags |= (MG_F_SEND_AND_CLOSE);
}

static void notify_timer_callback(void *ud)
{
  LOG(LL_INFO, ("Notify timer called"));
  notify_listeners(NOTIFY_TIMER);
}

// notify over mqtt, websocket and webhooks (status only)
static void notify_listeners(notify_type_t notify_reason)
{
  struct mg_mgr *mgr = mgos_get_mgr();
  
  struct mbuf response_buffer __attribute__((__cleanup__(cleanup_mbuf)));
  struct mbuf response_raw_buffer __attribute__((__cleanup__(cleanup_mbuf)));

  static time_t last_notify_timer;

  mgos_clear_timer(notify_timer_id);

  if(last_notify_timer == sensor_info.timestamp) {
    sensor_info.timestamp = time(NULL);
  }
  last_notify_timer = sensor_info.timestamp;

  mbuf_init(&response_buffer, 1024);
  getSatusAsJSON(&response_buffer);

  mbuf_init(&response_raw_buffer, 1024);
  getRawAsJSON(&response_raw_buffer);

#ifdef MGOS_CONFIG_HAVE_MQTT_STATUS_TOPIC
  // MQTT notify
  if (!mgos_mqtt_global_is_connected() || strlen(mgos_sys_config_get_mqtt_status_topic()) == 0)
    goto notify_http;

  if(notify_reason != NOTIFY_RAW) {
    mgos_mqtt_pub(mgos_sys_config_get_mqtt_status_topic(), response_buffer.buf, response_buffer.len, 1, false);
  }

  if(notify_reason == NOTIFY_RAW) {
    mgos_mqtt_pub(mgos_sys_config_get_mqtt_raw_topic(), response_raw_buffer.buf, response_raw_buffer.len, 1, false);
  }

  notify_http:
#endif

  // WS notify
  for (struct mg_connection *c = mgr->active_connections; c != NULL; c = mg_next(mgr, c))
  {
    if((c->flags & MG_F_IS_WEBSOCKET) == 0) continue;
    if((int)c->user_data == WS_ENDPOINT_STATUS && notify_reason != NOTIFY_RAW) {
      mg_send_websocket_frame(c, WEBSOCKET_OP_TEXT | WEBSOCKET_OP_CONTINUE, response_buffer.buf, response_buffer.len);
    }

    if((int)c->user_data == WS_ENDPOINT_RAW && notify_reason == NOTIFY_RAW) {
      mg_send_websocket_frame(c, WEBSOCKET_OP_TEXT | WEBSOCKET_OP_CONTINUE, response_raw_buffer.buf, response_raw_buffer.len);
    }

  }

#ifdef MGOS_CONFIG_HAVE_WEBHOOK
  // Webbhook notify
  const char *webhook_url = mgos_sys_config_get_webhook_url();
  if (webhook_url == NULL) goto notify_out;
  LOG(LL_INFO, ("Notify WebHook URL:  %s", webhook_url));

  void webhook_handler(struct mg_connection * c, int ev, void *evd,
                      void *cb_arg)
  {
    switch (ev)
    {
    case MG_EV_CLOSE /* constant-expression */:
      /* code */
      break;
    case MG_EV_TIMER /* handle timeouts */:
      LOG(LL_INFO, ("Webhook timed out!"));
      break;
    default:
      break;
    }
  }

  char *post_data __attribute__ ((__cleanup__(cleanup_post_data))) = malloc(response_buffer.len + 1);
  strncpy(post_data, response_buffer.buf, response_buffer.len);
  post_data[response_buffer.len] = '\0';
  mg_connect_http(mgr, webhook_handler, NULL, mgos_sys_config_get_webhook_url(), JSON_HEADERS, post_data);

  notify_out:
#endif

  notify_timer_id = mgos_set_timer(notify_timer_period_msec, 0, notify_timer_callback, NULL);
}

static void bme280_cb(int ev, void *evd, void *user_data UNUSED_ARG)
{
  if(ev != ENV_MEASUREMENT) return;
  struct mgos_bme280_data *environment_status = evd;
  LOG(LL_DEBUG, ("[BME read] temp %f, press %f, humid %f", environment_status->temp, environment_status->press, environment_status->humid));
  sensor_info.timestamp = time(NULL);
  sensor_info.air_temperature = environment_status->temp;
  sensor_info.air_pressure = environment_status->press;
  sensor_info.air_humidity = environment_status->humid;
}

static void pressure_cb(int ev, void *evd, void *user_data UNUSED_ARG)
{
  // skip anything but pressure measurement
  if(ev != PRESSURE_MEASUREMENT) return;
  pressure_status_t *pressure_status = evd;
  sensor_raw.timestamp = time(NULL);
  sensor_raw.tank_pressure_adc = pressure_status->raw_adc;
  notify_listeners(NOTIFY_RAW);
}

static void tank_volume_cb(int ev, void *evd, void *user_data UNUSED_ARG)
{
  // skip anything but valid measurements
  if(ev != VOLUME_MEASUREMENT) return;
  tank_volume_t *tank_volume_measurement = (tank_volume_t *)evd;
  sensor_info.timestamp = time(NULL);
  sensor_info.tank_liters = tank_volume_measurement->tank_liters;
  sensor_info.tank_percentage = tank_volume_measurement->tank_percentage;

  // text key representing status will be added in the
  // JSON preparation function

  tank_status_t tank_status = TANK_NORMAL;
  if (sensor_info.tank_liters < liters_low_value)
  {
    tank_status = TANK_LOW;
  }
  if (sensor_info.tank_liters > liters_high_value)
  {
    tank_status = TANK_FULL;
  }
  sensor_info.tank_status = tank_status;

  notify_listeners(NOTIFY_STATUS);
}

static void counter_cb(int ev, void *evd, void *user_data UNUSED_ARG)
{
  if(ev != COUNTER_CHANGE) return;

  gpio_counter_t *gpio_counter = evd;
  LOG(LL_DEBUG, ("COUNTER: Count %d, Frequency %d", gpio_counter->count, gpio_counter->frequency));

  if (sensor_raw.counter_count == gpio_counter->count && sensor_raw.counter_frequency == gpio_counter->frequency) return;

  sensor_raw.timestamp = time(NULL);
  sensor_raw.counter_count = gpio_counter->count;
  sensor_raw.counter_frequency = gpio_counter->frequency;
  notify_listeners(NOTIFY_RAW);

  if(freq_thr_hz == 0) return;

  bool tank_overflow = false;
  if (gpio_counter->frequency >= freq_thr_hz)
  {
    tank_overflow = true;
  }
  if(sensor_info.tank_overflow == tank_overflow) return;
  
  sensor_info.timestamp = time(NULL);
  sensor_info.tank_overflow = tank_overflow;
  notify_listeners(NOTIFY_STATUS);
}

// set new limits and store them in device config
static void pressure_set_limits_handler(struct mg_rpc_request_info *ri, void *cb_arg UNUSED_ARG,
                                        struct mg_rpc_frame_info *fi UNUSED_ARG, struct mg_str args)
{
  int32_t low_pressure_adc_val = -1, high_pressure_adc_val = -1;
  if (json_scanf(args.p, args.len,
                 ri->args_fmt,
                 &low_pressure_adc_val,
                 &high_pressure_adc_val) < 2)
  {
    LOG(LL_INFO, ("%s, [Set Limits] Error in request", TAG));
    mg_rpc_send_errorf(ri, 500, "Bad request. Expected {\"low_thr\":N, \"high_thr\":N}");
    return;
  }
  LOG(LL_INFO, ("%s, [Pressure limits] low: %d, high: %d", TAG, low_pressure_adc_val, high_pressure_adc_val));

  if (low_pressure_adc_val < 0 || high_pressure_adc_val > 4096 || low_pressure_adc_val > high_pressure_adc_val)
  {
    mg_rpc_send_errorf(ri, 500, "Invalid values. low_thr can not be less than 0, high_thr can not be more than 4096, low_thr can not be more than high_thr");
    return;
  }

  mgos_sys_config_set_tank_adc_pressure_low_threshold(low_pressure_adc_val);
  mgos_sys_config_set_tank_adc_pressure_high_threshold(high_pressure_adc_val);

  char **msg = &(char *){0};
  if (save_cfg(&mgos_sys_config, msg))
  {
    pressure_low_value = low_pressure_adc_val;
    pressure_high_value = high_pressure_adc_val;
    mg_rpc_send_responsef(ri, "{status:%B}", true);
    tank_volume_set_threshold(pressure_low_value, pressure_high_value);
  }
  else
  {
    mg_rpc_send_errorf(ri, 500, "Could not save values in config");
  }
  free(*msg);
}

// set new limits and store them in device config
static void tank_set_limits_handler(struct mg_rpc_request_info *ri, void *cb_arg UNUSED_ARG,
                                    struct mg_rpc_frame_info *fi UNUSED_ARG, struct mg_str args)
{
  float low_liters_val = 0, high_liters_val = tank_maximum_liters;
  if (json_scanf(args.p, args.len,
                 ri->args_fmt,
                 &low_liters_val,
                 &high_liters_val) < 2)
  {
    LOG(LL_INFO, ("%s, [Set Limits] Error in request", TAG));
    mg_rpc_send_errorf(ri, 500, "Bad request. Expected {\"low_thr\":N, \"high_thr\":N}");
    return;
  }
  LOG(LL_INFO, ("%s, [Liters limits] low: %f, high: %f", TAG, low_liters_val, high_liters_val));

  if (low_liters_val < 0 || high_liters_val > tank_maximum_liters || low_liters_val > high_liters_val)
  {
    mg_rpc_send_errorf(ri, 500, "Invalid values. low_thr can not be less than 0, high_thr can not be more than %4.1f, low_thr can not be more than high_thr", tank_maximum_liters);
    return;
  }

  mgos_sys_config_set_tank_liters_low_threshold(low_liters_val);
  mgos_sys_config_set_tank_liters_high_threshold(high_liters_val);

  char **msg = &(char *){0};
  if (save_cfg(&mgos_sys_config, msg))
  {
    liters_low_value = low_liters_val;
    liters_high_value = high_liters_val;
    mg_rpc_send_responsef(ri, "{status:%B}", true);
  }
  else
  {
    mg_rpc_send_errorf(ri, 500, "Could not save values in config");
  }
  free(*msg);
}

// control counter, set threshold
static void counter_stop_handler(struct mg_rpc_request_info *ri, void *cb_arg UNUSED_ARG,
                                 struct mg_rpc_frame_info *fi UNUSED_ARG, struct mg_str args UNUSED_ARG)
{
  bool success = sensor_counter_stop();
  mg_rpc_send_responsef(ri, "{status:%B}", success);
}

static void counter_start_handler(struct mg_rpc_request_info *ri, void *cb_arg UNUSED_ARG,
                                  struct mg_rpc_frame_info *fi UNUSED_ARG, struct mg_str args UNUSED_ARG)
{
  bool success = sensor_counter_start();
  mg_rpc_send_responsef(ri, "{status:%B}", success);
}

// set new limits and store them in device config
static void counter_set_limits_handler(struct mg_rpc_request_info *ri, void *cb_arg UNUSED_ARG,
                                       struct mg_rpc_frame_info *fi UNUSED_ARG, struct mg_str args)
{
  int cfg_freq_thr_hz = -1;
  if (json_scanf(args.p, args.len,
                 ri->args_fmt,
                 &cfg_freq_thr_hz) < 1)
  {
    LOG(LL_INFO, ("%s, [Frequency limits] Error in request", TAG));
    mg_rpc_send_errorf(ri, 500, "Bad request. Expected {\"freq_thr\":N}");
    return;
  }
  if (cfg_freq_thr_hz < 0 || cfg_freq_thr_hz > max_freq_thr_hz)
  {
    LOG(LL_INFO, ("%s, [Frequency limits] Error in request", TAG));
    mg_rpc_send_errorf(ri, 500, "Bad request. Expected freq_thr in [0..%d]", max_freq_thr_hz);
    return;
  }

  LOG(LL_INFO, ("%s, [Frequency limits] frequency threshold: %d", TAG, cfg_freq_thr_hz));

  mgos_sys_config_set_tank_frequency_high_threshold(cfg_freq_thr_hz);

  char **msg = &(char *){0};
  if (save_cfg(&mgos_sys_config, msg))
  {
    freq_thr_hz = cfg_freq_thr_hz;
    mg_rpc_send_responsef(ri, "{status:%B}", true);
  }
  else
  {
    mg_rpc_send_errorf(ri, 500, "Could not save values in config");
  }
  free(*msg);
}

enum mgos_app_init_result mgos_app_init(void)
{
  // init the config values
  pressure_low_value = mgos_sys_config_get_tank_adc_pressure_low_threshold();
  pressure_high_value = mgos_sys_config_get_tank_adc_pressure_high_threshold();
  liters_low_value = mgos_sys_config_get_tank_liters_low_threshold();
  liters_high_value = mgos_sys_config_get_tank_liters_high_threshold();
  freq_thr_hz = mgos_sys_config_get_tank_frequency_high_threshold();


  LOG(LL_INFO, ("Config read"));
  if (!sensor_bme280_init())
    return MGOS_APP_INIT_ERROR;

  if (!sensor_pressure_init())
    return MGOS_APP_INIT_ERROR;

  if (!sensor_counter_init())
    return MGOS_APP_INIT_ERROR;

  sensor_counter_start();

  tank_volume_init(pressure_low_value, pressure_high_value);

  LOG(LL_INFO, ("Periphery started"));

  board_rgb = mgos_neopixel_create(mgos_sys_config_get_board_rgb_pin(), 1, MGOS_NEOPIXEL_ORDER_RGB);
  mgos_neopixel_set(board_rgb, 0, 0, 0, 0);
  mgos_neopixel_show(board_rgb);

  // use two handlers registrations to tag the ws connections
  mgos_register_http_endpoint(mgos_sys_config_get_http_status_url(), http_handler, (void *)WS_ENDPOINT_STATUS);
  mgos_register_http_endpoint(mgos_sys_config_get_http_raw_url(), http_handler, (void *)WS_ENDPOINT_RAW);

  mgos_event_add_group_handler(ENV_EVENT_BASE, bme280_cb, NULL);
  mgos_event_add_group_handler(PRESSURE_EVENT_BASE, pressure_cb, NULL);
  mgos_event_add_group_handler(VOLUME_EVENT_BASE, tank_volume_cb, NULL);
  mgos_event_add_group_handler(COUNTER_EVENT_BASE, counter_cb, NULL);

  // Set the rpc methods for this application
  struct mg_rpc *c = mgos_rpc_get_global();
  mg_rpc_add_handler(c, "Device.Status",
                     "{}", rpc_status_handler, NULL);
  mg_rpc_add_handler(c, "Pressure.SetLimits",
                     pressure_limits_fmt, pressure_set_limits_handler, NULL);
  mg_rpc_add_handler(c, "Tank.SetLimits",
                     tank_limits_fmt, tank_set_limits_handler, NULL);
  mg_rpc_add_handler(c, "Counter.Stop",
                     "", counter_stop_handler, NULL);
  mg_rpc_add_handler(c, "Counter.Start",
                     "", counter_start_handler, NULL);
  mg_rpc_add_handler(c, "Counter.SetLimits",
                     freq_thr_fmt, counter_set_limits_handler, NULL);

  notify_listeners(NOTIFY_TIMER);

  return MGOS_APP_INIT_SUCCESS;
}
