#include <impl/time.h>

#include <FreeRTOSConfig.h>
#include <portmacro.h>
#include <freertos/task.h>

void thread_sleep(time_t us) {
    vTaskDelay(pdUS_TO_TICKS(us));
}
