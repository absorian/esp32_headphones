//
// Created by ism on 01.06.2023.
//

#ifndef IMPL_H
#define IMPL_H

#ifdef ESP_PLATFORM

#define logi(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#ifdef LOG_DEBUG
#define logd(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
#else
#define logd(tag, fmt, ...)
#endif
#define loge(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#include "freertos_impl.h"

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
