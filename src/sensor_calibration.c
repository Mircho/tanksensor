#include "sensor_calibration.h"
#include "sensor_bme280.h"
#include "sensor_pressure.h"

#define TAG "Calibration"

// BME280 fires every 2.5s; alpha = 2.5 / (10*60) gives ~10-min EMA time constant
#define TREND_ALPHA      0.004f
// °C difference between current temp and slow EMA to confirm a phase
#define TREND_HYSTERESIS 0.5f
// minimum temperature range covered before reporting ready
#define MIN_TEMP_SWING   10.0f
#define MIN_R2           0.95f
#define MIN_SAMPLES      100

typedef enum { PHASE_UNKNOWN = 0, PHASE_COOLING, PHASE_WARMING } phase_t;

static float  slow_temp  = 0.0f;
static bool   slow_init  = false;
static float  latest_temp = 0.0f;
static phase_t phase = PHASE_UNKNOWN;

// online regression accumulators (double to avoid float precision loss)
static double sn = 0, st = 0, sa = 0, stt = 0, sta = 0, saa = 0;
static float  t_min = 1000.0f, t_max = -1000.0f;

static calibration_status_t cal_status = {0};

static void reset_regression(void) {
  sn = st = sa = stt = sta = saa = 0;
  t_min = 1000.0f;
  t_max  = -1000.0f;
  bool was_ready = cal_status.ready;
  cal_status = (calibration_status_t){0};
  if (was_ready) mgos_event_trigger(CALIBRATION_UPDATED, &cal_status);
  LOG(LL_INFO, ("%s, new warming phase — regression reset", TAG));
}

static void accumulate(float temp, float adc) {
  sn  += 1.0;
  st  += temp;
  sa  += adc;
  stt += (double)temp * temp;
  sta += (double)temp * adc;
  saa += (double)adc  * adc;
  if (temp < t_min) t_min = temp;
  if (temp > t_max) t_max = temp;

  if (sn < 2) return;

  double denom = sn * stt - st * st;
  if (denom < 1e-10) return;

  double slope = (sn * sta - st * sa) / denom;

  // R² via Pearson: r² = (n·Σxy - Σx·Σy)² / ((n·Σxx - (Σx)²)(n·Σyy - (Σy)²))
  double r2_num = sn * sta - st * sa;
  double r2_denom_sq = (sn * stt - st * st) * (sn * saa - sa * sa);
  double r2 = (r2_denom_sq > 0) ? (r2_num * r2_num) / r2_denom_sq : 0.0;

  bool was_ready = cal_status.ready;

  cal_status.slope        = (float)slope;
  cal_status.r2           = (float)r2;
  cal_status.n_samples    = (int)sn;
  cal_status.temp_min     = t_min;
  cal_status.temp_max     = t_max;
  cal_status.last_updated = time(NULL);
  cal_status.ready        = (t_max - t_min >= MIN_TEMP_SWING)
                          && (r2 >= MIN_R2)
                          && (sn >= MIN_SAMPLES);

  if (cal_status.ready != was_ready) {
    LOG(LL_INFO, ("%s, ready=%d slope=%.4f r2=%.4f n=%d range=%.1f–%.1f°C",
        TAG, cal_status.ready, cal_status.slope, cal_status.r2,
        cal_status.n_samples, t_min, t_max));
    mgos_event_trigger(CALIBRATION_UPDATED, &cal_status);
  }
}

static void bme280_cb(int ev, void *evd, void *user_data UNUSED_ARG) {
  if (ev != ENV_MEASUREMENT) return;
  struct mgos_bme280_data *env = evd;
  float temp = env->temp;
  latest_temp = temp;

  if (!slow_init) {
    slow_temp = temp;
    slow_init = true;
    return;
  }

  slow_temp += TREND_ALPHA * (temp - slow_temp);

  float diff = temp - slow_temp;
  phase_t new_phase = phase;
  if      (diff >  TREND_HYSTERESIS) new_phase = PHASE_WARMING;
  else if (diff < -TREND_HYSTERESIS) new_phase = PHASE_COOLING;

  if (new_phase != phase) {
    LOG(LL_INFO, ("%s, phase %s → %s (temp=%.2f slow=%.2f)",
        TAG,
        phase == PHASE_UNKNOWN ? "unknown" : phase == PHASE_COOLING ? "cooling" : "warming",
        new_phase == PHASE_COOLING ? "cooling" : "warming",
        temp, slow_temp));
    if (new_phase == PHASE_WARMING) reset_regression();
    phase = new_phase;
  }
}

static void pressure_cb(int ev, void *evd, void *user_data UNUSED_ARG) {
  if (ev != PRESSURE_MEASUREMENT) return;
  if (phase != PHASE_WARMING) return;
  pressure_status_t *ps = evd;
  accumulate(latest_temp, (float)ps->raw_adc);
}

void sensor_calibration_init(void) {
  mgos_event_register_base(CALIBRATION_EVENT_BASE, "Calibration events");
  mgos_event_add_group_handler(ENV_EVENT_BASE, bme280_cb, NULL);
  mgos_event_add_group_handler(PRESSURE_EVENT_BASE, pressure_cb, NULL);
}

const calibration_status_t *sensor_calibration_get_status(void) {
  return &cal_status;
}
