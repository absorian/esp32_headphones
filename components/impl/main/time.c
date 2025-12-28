#include <impl/time.h>


time_t time_micros() {
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return time_us_from_timespec(&spec);
}

struct timespec time_timespec_from_us(time_t us) {
    struct timespec spec = {
        .tv_nsec = (us % (time_t) 1e6L) * 1e3L,
        .tv_sec = us / 1e6L
    };
    return spec;
}

time_t time_us_from_timespec(const struct timespec *spec) {
    return spec->tv_sec * 1e6L + spec->tv_nsec / (time_t) 1e3L;
}
