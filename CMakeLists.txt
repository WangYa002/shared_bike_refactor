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

# asio is header-only. vcpkg supplies a cmake config; Ubuntu's libasio-dev does not.
# Provide a fallback INTERFACE target so find_package(asio) isn't required downstream.
if(NOT TARGET asio::asio)
    find_path(ASIO_HEADER_PATH asio.hpp PATH_SUFFIXES asio)
    if(NOT ASIO_HEADER_PATH)
        message(FATAL_ERROR "asio.hpp not found — install libasio-dev / asio via vcpkg, or set ASIO_HEADER_PATH")
    endif()
    add_library(bike_asio_iface INTERFACE)
    target_include_directories(bike_asio_iface INTERFACE ${ASIO_HEADER_PATH})
    target_compile_definitions(bike_asio_iface INTERFACE ASIO_STANDALONE)
    add_library(asio::asio ALIAS bike_asio_iface)
endif()

add_subdirectory(common)
add_subdirectory(server)
if(BIKE_BUILD_CLIENT)
    add_subdirectory(client)
endif()
