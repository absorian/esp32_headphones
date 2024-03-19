cmake_minimum_required(VERSION 3.9)

set(SOURCES_COMMON net_controller.cpp receiver.cpp sender.cpp)

if (ESP_PLATFORM EQUAL 1)
    idf_component_register(SRCS ${SOURCES_COMMON}
            REQUIRES impl
            INCLUDE_DIRS "./include"
            PRIV_INCLUDE_DIRS "./private"
            )
else ()
    add_library(net_controller STATIC ${SOURCES_COMMON})

    target_link_libraries(net_controller impl)

    target_include_directories(net_controller PUBLIC ./include)
    target_include_directories(net_controller PRIVATE ./private)
endif ()
