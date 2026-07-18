cmake_minimum_required(VERSION 3.20)
project(shared_bike_refactor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(BIKE_BUILD_TESTS "Build server tests" ON)
option(BIKE_BUILD_CLIENT "Build Qt6 client" OFF)

add_subdirectory(common)
add_subdirectory(server)
if(BIKE_BUILD_CLIENT)
    add_subdirectory(client)
endif()
