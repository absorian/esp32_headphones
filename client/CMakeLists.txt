cmake_minimum_required(VERSION 3.16)

set(COMPONENTS_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../components)

include($ENV{ADF_PATH}/CMakeLists.txt)

list(APPEND EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)
list(APPEND EXTRA_COMPONENT_DIRS ${COMPONENTS_DIRECTORY})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(client)
