#pragma once

#include "defines.h"

typedef struct clock {
  f64 start_time;
  f64 elapsed;
} clock;

/* Update specified clock. Should be called just before checking elapsed time. */
void clock_update(clock * clock);
/* Start specified clock after resetting elapsed time */
void clock_start(clock * clock);
/* Stop specified clock without resetting elapsed time */
void clock_stop(clock * clock);