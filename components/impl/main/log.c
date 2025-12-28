#include <impl/log.h>
#include <impl/time.h>

time_t log_timestamp() {
    return time_micros() / 1e3L;
}
