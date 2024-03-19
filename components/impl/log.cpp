#include <impl/concurrency.h>
#include <ctime>

time_t log_timestamp() {
    return thread_millis();
}
