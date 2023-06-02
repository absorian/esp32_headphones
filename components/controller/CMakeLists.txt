cmake_minimum_required(VERSION 3.9)

if (ESP_PLATFORM EQUAL 1)
    idf_component_register(SRCS "controller.cpp"
            REQUIRES impl
            INCLUDE_DIRS "."
            )
else ()
    add_library(controller controller.cpp)

    target_link_libraries(controller impl)
    target_include_directories(controller PUBLIC .)
endif ()
