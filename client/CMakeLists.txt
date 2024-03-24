cmake_minimum_required(VERSION 3.16)

set(COMPONENTS_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../components)

list(APPEND EXTRA_COMPONENT_DIRS ${COMPONENTS_DIRECTORY})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(client)
