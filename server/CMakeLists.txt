cmake_minimum_required(VERSION 3.28)

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

set(CMAKE_C_STANDARD 11)

find_package(portaudio CONFIG REQUIRED)

subdir_list(comps ${COMPONENTS_DIRECTORY})
foreach (c ${comps})
    add_subdirectory(${COMPONENTS_DIRECTORY}/${c} ${CMAKE_CURRENT_BINARY_DIR}/components/${c})
endforeach ()

include_directories(${portaudio_INCLUDE_DIRS})

add_executable(${CMAKE_PROJECT_NAME} main.cpp)

target_link_libraries(${CMAKE_PROJECT_NAME} portaudio_static)

foreach (c ${comps})
    target_link_libraries(${CMAKE_PROJECT_NAME} ${c})
endforeach ()
