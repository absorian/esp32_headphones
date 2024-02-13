//
// Created by ism on 01.06.2023.
//

#ifndef IMPL_H
#define IMPL_H

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

#ifdef ESP_PLATFORM

#include "freertos_impl.h"

#else

#include "posix_impl.h"

#endif

#include "asio_impl.h"

#endif //IMPL_H
