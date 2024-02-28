#ifndef COMMON_UTIL_H
#define COMMON_UTIL_H

#include <stdint-gcc.h>
#include <esp_event.h>

ESP_EVENT_DECLARE_BASE(APPLICATION);

#define BAD_VAL_CHECK(x, b) if (x == b) { loge(TAG, #x " has bad value = " #b); }
#define BAD_VAL_MSG(x, b, m) if (x == b) { loge(TAG, #x " has bad value = " #b ": " m); }
#define BAD_VAL_MSG_IF(x, b, m) BAD_VAL_MSG(x, b, m) if(x == b)
#define BAD_VAL_IF(x, b) BAD_VAL_CHECK(x, b) if(x == b)

long map(long x, long in_min, long in_max, long out_min, long out_max);

#endif //COMMON_UTIL_H
