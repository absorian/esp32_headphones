cmake_minimum_required(VERSION 3.17)

set(COMPONENTS_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../components)

MACRO(subdir_list result curdir)
    FILE(GLOB children RELATIVE ${curdir} ${curdir}/*)
    SET(dirlist "")
    FOREACH(child ${children})
        IF(IS_DIRECTORY ${curdir}/${child})
            LIST(APPEND dirlist ${child})
        ENDIF()
    ENDFOREACH()
    SET(${result} ${dirlist})
ENDMACRO()

project(server)

set(CMAKE_CXX_STANDARD 14)

#add_subdirectory($ENV{SFML} ${CMAKE_CURRENT_BINARY_DIR}/sfml)
add_subdirectory($ENV{ASIO} ${CMAKE_CURRENT_BINARY_DIR}/asio)
add_subdirectory($ENV{PORTAUDIO} ${CMAKE_CURRENT_BINARY_DIR}/portaudio)

subdir_list(comps ${COMPONENTS_DIRECTORY})
foreach (c ${comps})
    add_subdirectory(${COMPONENTS_DIRECTORY}/${c} ${CMAKE_CURRENT_BINARY_DIR}/components/${c})
endforeach ()

add_executable(${CMAKE_PROJECT_NAME} main.cpp)

target_link_libraries(${CMAKE_PROJECT_NAME} PortAudio)


#target_link_libraries(${CMAKE_PROJECT_NAME} sfml-audio #[[FLAC]])
foreach (c ${comps})
    target_link_libraries(${CMAKE_PROJECT_NAME} ${c})
endforeach ()

#target_compile_features(${CMAKE_PROJECT_NAME} PUBLIC cxx_std_14)

set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND_STRING PROPERTY COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)

