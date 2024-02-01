//
// Created by ism on 01.06.2023.
//

#include "asio_impl.h"

asio::io_context io_context_glob;

// Workaround for asio port
// https://github.com/espressif/esp-idf/issues/3557
#ifdef ESP_IDF_VERSION
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
char* if_indextoname(unsigned int , char* ) { return nullptr; }
unsigned int if_nametoindex(const char *ifname) { return 0; }
#endif
#endif