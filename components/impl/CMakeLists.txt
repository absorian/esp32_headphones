cmake_minimum_required(VERSION 3.9)

if (ESP_PLATFORM EQUAL 1)
    idf_component_register(SRCS "esp_impl.cpp"
            REQUIRES log esp_event asio
            INCLUDE_DIRS "."
            )
else()
    add_library(impl INTERFACE)

    target_link_libraries(impl INTERFACE sfml-network)
    target_include_directories(impl INTERFACE .)
endif ()
