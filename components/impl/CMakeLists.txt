cmake_minimum_required(VERSION 3.9)

if (ESP_PLATFORM EQUAL 1)
    idf_component_register(SRCS "asio_io_context.cpp"
            REQUIRES log esp_event asio
            INCLUDE_DIRS "."
            )
else()
    add_library(impl asio_io_context.cpp)

    target_link_libraries(impl sfml-system asio)
    target_include_directories(impl PUBLIC .)
endif ()
