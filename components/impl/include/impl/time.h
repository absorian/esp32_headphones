#pragma once

#include <time.h>

#ifdef ESP_PLATFORM
#define pdUS_TO_TICKS(xTimeInUs) ((TickType_t)((TickType_t)xTimeInUs * (TickType_t)configTICK_RATE_HZ))

#endif

struct timespec time_timespec_from_us(time_t us);

time_t time_us_from_timespec(const struct timespec *spec);

time_t time_micros();

void time_sleep(time_t us);
