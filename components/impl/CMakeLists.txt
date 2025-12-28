cmake_minimum_required(VERSION 3.9)

set(CMAKE_C_STANDARD 11)

if (ESP_PLATFORM EQUAL 1)
    idf_component_register(SRCS
            main/log.c main/socket.c main/time.c
            esp/concurrency.c esp/queue.c esp/time.c
            PRIV_REQUIRES freertos esp_netif
            INCLUDE_DIRS "./include" "./esp/include"
    )
else ()
    add_library(impl STATIC
            main/log.c main/socket.c main/time.c
            posix/concurrency.c posix/queue.c posix/time.c
    )

    if (WIN32)
        target_link_libraries(impl wsock32 ws2_32)
    endif ()

    target_include_directories(impl PUBLIC
            ./include
            ./posix/include)
endif ()
