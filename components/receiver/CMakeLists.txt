cmake_minimum_required(VERSION 3.9)

if (ESP_PLATFORM EQUAL 1)
    idf_component_register(SRCS "receiver.cpp"
            REQUIRES controller impl
            INCLUDE_DIRS "."
            )
else()
    add_library(receiver receiver.cpp)

    target_link_libraries(receiver controller impl)
    target_include_directories(receiver PUBLIC .)
endif ()
