#ifndef COMMON_UTIL_H
#define COMMON_UTIL_H

#include <stdint-gcc.h>
#include <esp_event.h>

ESP_EVENT_DECLARE_BASE(APPLICATION);

long map(long x, long in_min, long in_max, long out_min, long out_max);

#endif //COMMON_UTIL_H
