#ifndef IMPL_LOG_H
#define IMPL_LOG_H

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

#include <ctime>
#include <cstdio>
extern time_t log_timestamp();

#define logi(tag, fmt, ...) printf(LOG_COLOR(LOG_COLOR_GREEN) "I (%lld) %s: " fmt LOG_RESET_COLOR "\n", log_timestamp(), tag, ##__VA_ARGS__)
#define logp(tag, fmt, ...) printf(LOG_BOLD(LOG_COLOR_BLUE) "P %s: " fmt LOG_RESET_COLOR "\n", tag, ##__VA_ARGS__)
#define loge(tag, fmt, ...) printf(LOG_COLOR(LOG_COLOR_RED) "E (%lld) %s: " fmt LOG_RESET_COLOR "\n", log_timestamp(), tag, ##__VA_ARGS__)
#define logr(fmt, ...) printf(fmt, ##__VA_ARGS__); fflush(stdout)

#endif //IMPL_LOG_H
