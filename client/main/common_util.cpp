#include <sys/time.h>
#include "common_util.h"

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

int64_t get_time_ms() {
    timeval tv_now{};
    gettimeofday(&tv_now, nullptr);
    return (int64_t) tv_now.tv_sec * 1000L + (int64_t) tv_now.tv_usec / 1000L;
}