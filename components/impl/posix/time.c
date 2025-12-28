#include <impl/time.h>


void time_sleep(time_t us) {
    const struct timespec t = time_timespec_from_us(us);
    nanosleep(&t, NULL);
}
