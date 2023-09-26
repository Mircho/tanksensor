#include "stdlib.h"
#include "stdio.h"
#include "assert.h"

#include "sensor.h"

void add_filter(observable_value_t *ov, filter_item_t *fi)
{
  filter_item_t **current_filter = &ov->filters;

  while ((*current_filter) != NULL)
  {
    current_filter = &(*current_filter)->next;
  }

  *current_filter = fi;
}

void remove_filter(observable_value_t *ov, filter_item_t *fi)
{
  filter_item_t **current_filter = &ov->filters;

  if ((*current_filter) == NULL)
    return;

  if (*current_filter == fi)
  {
    *current_filter = (*current_filter)->next;
    return;
  }

  while ((*current_filter)->next != NULL)
  {
    if ((*current_filter)->next == fi)
    {
      (*current_filter)->next = (*current_filter)->next;
      return;
    }

    current_filter = &(*current_filter)->next;
  };
}

filter_ret_val_t filter_item_clamp_fn(filter_item_t *this, observable_number_t *var)
{
  filter_item_clamp_t *fi = (filter_item_clamp_t *)this;
  if (var->value < fi->min)
    var->value = fi->min;
  if (var->value > fi->max)
    var->value = fi->max;
  return FILTER_CONTINUE;
}

filter_ret_val_t filter_item_offset_fn(filter_item_t *this, observable_number_t *var)
{
  filter_item_offset_t *fi = (filter_item_offset_t *)this;
  var->value += fi->offset;
  return FILTER_CONTINUE;
}

filter_ret_val_t filter_item_skip_fn(filter_item_t *this, observable_number_t *var)
{
  filter_item_skip_t *fi = (filter_item_skip_t *)this;
  if (fi->skip > 0)
  {
    fi->skip--;
    return FILTER_STOP;
  }
  return FILTER_CONTINUE;
}

filter_ret_val_t filter_item_exp_moving_average_fn(filter_item_t *this, observable_number_t *var)
{
  filter_item_exp_moving_average_t *fi = (filter_item_exp_moving_average_t *)this;
  if (fi->initialized == false)
  {
    fi->previous_value = var->value;
    fi->initialized = true;
  }
  if (fi->pass_first)
  {
    fi->pass_first = false;
    return FILTER_CONTINUE;
  }
  var->value = fi->previous_value + fi->alpha * (var->value - fi->previous_value);
  fi->previous_value = var->value;
  return FILTER_CONTINUE;
}

filter_ret_val_t filter_item_average_fn(filter_item_t *this, observable_number_t *var)
{
  filter_item_average_t *fi = (filter_item_average_t *)this;
  assert(fi->number_of_samples > 0);
  fi->accumulator_ += var->value;
  fi->sample_counter_--;
  if (fi->sample_counter_ > 0)
    return FILTER_STOP;

  var->value = fi->accumulator_ / fi->number_of_samples;
  fi->accumulator_ = 0;
  fi->sample_counter_ = fi->number_of_samples;

  return FILTER_CONTINUE;
}

filter_ret_val_t filter_item_linear_fit_fn(filter_item_t *this, observable_number_t *var)
{
  filter_item_linear_fit_t *fi = (filter_item_linear_fit_t *)this;
  assert(fi->value_map[0][0] != fi->value_map[1][0]);
  // y1-y2/x1-x2
  // float slope = (fi->value_map[0][1] - fi->value_map[1][1]) / (fi->value_map[0][0] - fi->value_map[1][0]);
  // y1 - slope*x1
  // float intercept = fi->value_map[0][1] - slope * fi->value_map[0][0];
  var->value = fi->slope_ * var->value + fi->intercept_;

  return FILTER_CONTINUE;
}

// observer methods
// same callback is allowed multiple times
void add_observer(observable_value_t *ov, value_observer_cb observer_cb)
{
  value_observer_item_t **current_observer = &ov->observers;

  value_observer_item_t *new_observer_item = (value_observer_item_t *)malloc(sizeof(value_observer_item_t));
  if (new_observer_item == NULL)
    return;
  new_observer_item->observer = observer_cb;
  new_observer_item->next = NULL;

  if ((*current_observer) == NULL)
  {
    *current_observer = new_observer_item;
    return;
  }

  while ((*current_observer)->next != NULL)
  {
    current_observer = &(*current_observer)->next;
  }
  (*current_observer)->next = new_observer_item;
}

void remove_observer(observable_value_t *ov, value_observer_cb observer_cb)
{
  value_observer_item_t **current_observer = &ov->observers;

  if ((*current_observer) == NULL)
    return;

  if ((*current_observer)->observer == observer_cb)
  {
    value_observer_item_t *rm = (*current_observer);
    *current_observer = (*current_observer)->next;
    free(rm);
    return;
  }

  while ((*current_observer)->next != NULL)
  {
    if ((*current_observer)->next->observer == observer_cb)
    {
      value_observer_item_t *rm = (*current_observer)->next;
      (*current_observer)->next = (*current_observer)->next->next;
      free(rm);
      return;
    }
    current_observer = &(*current_observer)->next;
  }
}

void cleanup_observers(observable_value_t *ov)
{
  value_observer_item_t *current_observer = ov->observers;
  value_observer_item_t *next_observer;

  if (current_observer == NULL)
    return;

  while (current_observer != NULL)
  {
    next_observer = current_observer->next;
    free(current_observer);
    current_observer = next_observer;
  }
}

void notify_observers(observable_value_t *ov)
{
  value_observer_item_t *current_observer = ov->observers;
  while (current_observer != NULL)
  {
    current_observer->observer(ov);
    current_observer = current_observer->next;
  }
}

// value methods
void set_value(observable_value_t *ov, observable_number_t new_value)
{
  filter_item_t *current_filter = ov->filters;
  bool accept_new_value = true;
  while (current_filter != NULL)
  {
    if (current_filter->filter(current_filter, &new_value) != FILTER_CONTINUE)
    {
      accept_new_value = false;
      break;
    }
    current_filter = current_filter->next;
  }
  if (accept_new_value == true)
  {
    ov->value = new_value;
    ov->notify(ov);
  }
}

void process_new_value(observable_value_t *ov, number_type new_value) {
  observable_number_t value_ = {
    .value = new_value
  };
  ov->set(ov, value_);
}