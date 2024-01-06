//
// Created by ism on 01.06.2023.
//

#ifndef IMPL_H
#define IMPL_H

#ifdef ESP_PLATFORM

#define logi(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#ifdef LOG_DEBUG
#define logd(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#else
#define logd(tag, fmt, ...)
#endif
#define loge(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#include "freertos_impl.h"

// Workaround for asio port
// https://github.com/espressif/esp-idf/issues/3557
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
char* if_indextoname(unsigned int , char* ) { return nullptr; }
unsigned int if_nametoindex(const char *ifname) { return 0; }
#endif

#else

#define logi(tag, fmt, ...) printf("I|%s: " fmt "\n", tag, ##__VA_ARGS__)
#ifdef LOG_DEBUG
#define logd(tag, fmt, ...) printf("D|%s: " fmt "\n", tag, ##__VA_ARGS__)
#else
#define logd(tag, fmt, ...)
#endif
#define loge(tag, fmt, ...) fprintf(stderr, "E|%s: " fmt "\n", tag, ##__VA_ARGS__)
#include "pthread_impl.h"

#endif

#include "asio_impl.h"

#endif //IMPL_H
