cmake_minimum_required(VERSION 3.9)

set(SOURCES_COMMON log.cpp socket.cpp)

if (ESP_PLATFORM EQUAL 1)
    idf_component_register(SRCS ${SOURCES_COMMON} concurrency_freertos.cpp
            PRIV_REQUIRES freertos esp_netif
            INCLUDE_DIRS "./include"
            # PRIV_INCLUDE_DIRS "./private"
            )
else ()
    add_library(impl STATIC ${SOURCES_COMMON} concurrency_posix.cpp)

    if(WIN32)
        target_link_libraries(impl wsock32 ws2_32)
    endif()

    target_include_directories(impl PUBLIC ./include)
    target_include_directories(impl PRIVATE ./private)
endif ()
