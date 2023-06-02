cmake_minimum_required(VERSION 3.9)

if (ESP_PLATFORM EQUAL 1)
    idf_component_register(SRCS "sender.cpp"
            REQUIRES controller impl
            INCLUDE_DIRS "."
            )
else()
    add_library(sender sender.cpp)

    target_link_libraries(sender controller impl)
    target_include_directories(sender PUBLIC .)
endif ()
