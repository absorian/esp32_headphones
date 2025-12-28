#pragma once

#define LOG_COLOR_BLACK   "30"
#define LOG_COLOR_RED     "31"
#define LOG_COLOR_GREEN   "32"
#define LOG_COLOR_BROWN   "33"
#define LOG_COLOR_BLUE    "34"
#define LOG_COLOR_PURPLE  "35"
#define LOG_COLOR_CYAN    "36"
#define LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
#define LOG_BOLD(COLOR)   "\033[1;" COLOR "m"
#define LOG_RESET_COLOR   "\033[0m"

#include <time.h>
#include <stdio.h>

time_t log_timestamp();

#ifdef APP_DEBUG
// Debug
#define logd(fmt, ...) \
    printf(LOG_COLOR(LOG_COLOR_PURPLE) "D (%lld) [%s] " fmt LOG_RESET_COLOR "\n", \
    log_timestamp(), __func__, ##__VA_ARGS__)
#else
#define logd(fmt, ...)
#endif

// Info
#define logi(fmt, ...) \
    printf(LOG_COLOR(LOG_COLOR_GREEN) "I (%lld) [%s] " fmt LOG_RESET_COLOR "\n", \
    log_timestamp(), __func__, ##__VA_ARGS__)
// Warning
#define logw(fmt, ...) \
    printf(LOG_BOLD(LOG_COLOR_BLUE) "W (%lld) [%s] " fmt LOG_RESET_COLOR "\n", \
    log_timestamp(), __func__, ##__VA_ARGS__)
// Error
#define loge(fmt, ...) \
    printf(LOG_COLOR(LOG_COLOR_RED) "E (%lld) [%s] " fmt LOG_RESET_COLOR "\n", \
    log_timestamp(), __func__, ##__VA_ARGS__)
// Raw
#define logr(fmt, ...) \
    printf(fmt, ##__VA_ARGS__); fflush(stdout)
