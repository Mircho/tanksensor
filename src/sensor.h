#pragma once

#include "stdlib.h"
#include "stdbool.h"
#include "stdint.h"

typedef struct observable_value observable_value_t;

typedef double number_type;

typedef struct observable_number observable_number_t;
struct observable_number
{
  number_type value;
};

typedef enum filter_ret_val filter_ret_val_t;
enum filter_ret_val
{
  FILTER_CONTINUE,
  FILTER_STOP,
  FILTER_INVALID_VALUE
};

typedef struct filter_item filter_item_t;
typedef filter_ret_val_t (*value_filter_fn)(filter_item_t *, observable_number_t *);
struct filter_item
{
  value_filter_fn filter;
  filter_item_t *next;
};

void add_filter(observable_value_t *ov, filter_item_t *fi);
void remove_filter(observable_value_t *ov, filter_item_t *fi);

typedef struct filter_item_exp_moving_average filter_item_exp_moving_average_t;
struct filter_item_exp_moving_average
{
  filter_item_t super;
  bool initialized;
  number_type previous_value;
  float alpha;
  bool pass_first;
};

typedef struct filter_item_average filter_item_average_t;
struct filter_item_average
{
  filter_item_t super;
  size_t number_of_samples;
  size_t sample_counter_;
  number_type accumulator_;
  bool pass_first;
};

// provide two points like value_map[0] = {500,0}, value_map[1] = {680,100}
typedef struct filter_item_linear_fit filter_item_linear_fit_t;
struct filter_item_linear_fit
{
  filter_item_t super;
  float value_map[2][2];
  float slope_;
  float intercept_;
};

typedef struct filter_item_offset filter_item_offset_t;
struct filter_item_offset
{
  filter_item_t super;
  number_type offset;
};

typedef struct filter_item_skip filter_item_skip_t;
struct filter_item_skip
{
  filter_item_t super;
  uint16_t skip;
};

typedef struct filter_item_clamp filter_item_clamp_t;
struct filter_item_clamp
{
  filter_item_t super;
  number_type min;
  number_type max;
};

filter_ret_val_t filter_item_linear_fit_fn(filter_item_t *this, observable_number_t *var);
filter_ret_val_t filter_item_exp_moving_average_fn(filter_item_t *this, observable_number_t *var);
filter_ret_val_t filter_item_average_fn(filter_item_t *this, observable_number_t *var);
filter_ret_val_t filter_item_clamp_fn(filter_item_t *this, observable_number_t *var);
filter_ret_val_t filter_item_offset_fn(filter_item_t *this, observable_number_t *var);
filter_ret_val_t filter_item_skip_fn(filter_item_t *this, observable_number_t *var);

typedef void (*value_observer_cb)(observable_value_t *);

typedef struct value_observer_item value_observer_item_t;
struct value_observer_item
{
  value_observer_cb observer;
  value_observer_item_t *next;
};

typedef void (*set_value_fn)(observable_value_t *, observable_number_t);
typedef void (*process_value_fn)(observable_value_t *, number_type);
typedef void (*notify_observers_fn)(observable_value_t *);

void add_observer(observable_value_t *ov, value_observer_cb observer_cb);
void remove_observer(observable_value_t *ov, value_observer_cb observer_cb);
void cleanup_observers(observable_value_t *ov);
void notify_observers(observable_value_t *ov);
void set_value(observable_value_t *ov, observable_number_t new_value);
void process_new_value(observable_value_t *ov, number_type new_value);

struct observable_value
{
  char name[20];
  observable_number_t value;
  float delta_thr;
  bool force_notify;
  filter_item_t *filters;
  value_observer_item_t *observers;
  set_value_fn set;
  process_value_fn process;
  notify_observers_fn notify;
};

#define OBSERVABLE_NUMBER(var_name, initial_value) \
  observable_number_t var_name = {                 \
      .value = initial_value,                      \
  }

#define SET_OBSERVABLE_VAR(var_name, new_value) \
  var_name.value = new_value;

// use gcc extension for deferred cleanup of type

#define OBSERVABLE_VALUE(var_name, initial_value)                                 \
  observable_value_t var_name __attribute__((__cleanup__(cleanup_observers))) = { \
      .value = initial_value,                                                     \
      .name = #var_name,                                                          \
      .force_notify = false,                                                      \
      .delta_thr = 0.0,                                                           \
      .filters = NULL,                                                            \
      .observers = NULL,                                                          \
      .set = set_value,                                                           \
      .process = process_new_value,                                               \
      .notify = notify_observers,                                                 \
  }

#define PROCESS_NEW_VALUE(var_name, var_value) \
  var_name.process(&var_name, var_value)

#define ADD_OBSERVER(var_name, observer) \
  add_observer(&var_name, observer)

#define REMOVE_OBSERVER(var_name, observer) \
  remove_observer(&var_name, observer)

#define CLEANUP_OBSERVERS(var_name) \
  cleanup_observers(&var_name)

#define NOTIFY_OBSERVERS(var_name) \
  var_name.notify(&var_name)

#define FILTER(filter_name, type)              \
  filter_item_##type##_t filter_name = {       \
      .super.filter = filter_item_##type##_fn, \
  }

#define FILTER_CLAMP(filter_name, min_val, max_val) \
  FILTER(filter_name, clamp);                       \
  filter_name.min = min_val;                        \
  filter_name.max = max_val;

#define FILTER_OFFSET(filter_name, offset_val) \
  FILTER(filter_name, offset);                 \
  filter_name.offset = offset_val;

#define FILTER_SKIP(filter_name, skip_val) \
  FILTER(filter_name, skip);               \
  filter_name.skip = skip_val;

#define FILTER_EXP_MOVING_AVERAGE(filter_name, alpha_val, pass_first_val) \
  FILTER(filter_name, exp_moving_average);                                \
  filter_name.initialized = false;                                        \
  filter_name.previous_value = 0;                                         \
  filter_name.alpha = alpha_val;                                          \
  filter_name.pass_first = pass_first_val;

#define FILTER_AVERAGE(filter_name, number_of_samples_val, pass_first_val) \
  FILTER(filter_name, average);                                            \
  filter_name.number_of_samples = number_of_samples_val;                   \
  filter_name.sample_counter_ = number_of_samples_val;                     \
  filter_name.accumulator_ = 0;                                            \
  filter_name.pass_first = pass_first_val;

#define FILTER_LINEAR_FIT(filter_name, from_1, to_1, from_2, to_2)                    \
  FILTER(filter_name, linear_fit);                                                    \
  filter_name.value_map[0][0] = from_1;                                               \
  filter_name.value_map[0][1] = to_1;                                                 \
  filter_name.value_map[1][0] = from_2;                                               \
  filter_name.value_map[1][1] = to_2;                                                 \
  filter_name.slope_ = ((float)to_1 - (float)to_2) / ((float)from_1 - (float)from_2); \
  filter_name.intercept_ = (float)to_1 - filter_name.slope_ * (float)from_1;

#define ADD_FILTER(var_name, filter) \
  add_filter(&var_name, (filter_item_t *)&filter);
