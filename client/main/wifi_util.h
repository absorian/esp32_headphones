#ifndef WIFI_UTIL_H
#define WIFI_UTIL_H

#include <esp_err.h>

namespace wifi_util {

    void init();

    void shutdown();

    esp_err_t connect();

}

#endif //WIFI_UTIL_H
