cmake_minimum_required(VERSION 3.16.0)

set(EXTRA_COMPONENT_DIRS "")

# Only add component directories that actually exist
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/components")
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
endif()

if(DEFINED ENV{MATTER_PATH} AND EXISTS "$ENV{MATTER_PATH}/components")
    list(APPEND EXTRA_COMPONENT_DIRS "$ENV{MATTER_PATH}/components")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart_home_IoT_idf)
