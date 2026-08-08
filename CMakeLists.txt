cmake_minimum_required(VERSION 3.20)
project(shared_bike_refactor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(BIKE_BUILD_TESTS "Build server tests" ON)
option(BIKE_BUILD_CLIENT "Build Qt6 client" OFF)
option(BIKE_BUILD_SERVER "Build bike-gateway/bike-dispatch binaries" ON)

# asio is vendored under third_party/asio (1.30.2); 模块二后仅 Qt 客户端仍使用
# (bike_server_net 已移除)。vcpkg's asio (when present) still wins via
# find_package elsewhere; this is the fallback.
if(NOT TARGET asio::asio)
    set(ASIO_HEADER_PATH ${CMAKE_SOURCE_DIR}/third_party/asio)
    if(NOT EXISTS ${ASIO_HEADER_PATH}/asio.hpp)
        message(FATAL_ERROR "vendored asio.hpp not found at ${ASIO_HEADER_PATH}")
    endif()
    add_library(bike_asio_iface INTERFACE)
    target_include_directories(bike_asio_iface INTERFACE ${ASIO_HEADER_PATH})
    target_compile_definitions(bike_asio_iface INTERFACE ASIO_STANDALONE)
    add_library(asio::asio ALIAS bike_asio_iface)
endif()

# hiredis: Ubuntu's libhiredis-dev ships headers + lib but no cmake config.
if(BIKE_BUILD_SERVER AND NOT TARGET hiredis::hiredis)
    find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
    find_library(HIREDIS_LIB NAMES hiredis)
    if(NOT HIREDIS_INCLUDE_DIR OR NOT HIREDIS_LIB)
        message(FATAL_ERROR "hiredis not found — install libhiredis-dev")
    endif()
    add_library(bike_hiredis_iface INTERFACE)
    target_include_directories(bike_hiredis_iface INTERFACE ${HIREDIS_INCLUDE_DIR})
    target_link_libraries(bike_hiredis_iface INTERFACE ${HIREDIS_LIB})
    add_library(hiredis::hiredis ALIAS bike_hiredis_iface)
endif()

# MySQL client: Ubuntu's libmysqlclient-dev ships headers + lib but no cmake config.
if(BIKE_BUILD_SERVER AND NOT TARGET MySQL::MySQL)
    find_path(MYSQL_INCLUDE_DIR mysql/mysql.h)
    find_library(MYSQL_LIB NAMES mysqlclient libmysql)
    if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIB)
        message(FATAL_ERROR "mysqlclient not found — install libmysqlclient-dev")
    endif()
    add_library(bike_mysql_iface INTERFACE)
    target_include_directories(bike_mysql_iface INTERFACE ${MYSQL_INCLUDE_DIR})
    target_link_libraries(bike_mysql_iface INTERFACE ${MYSQL_LIB})
    add_library(MySQL::MySQL ALIAS bike_mysql_iface)
endif()

# liburing: Linux-only 网关依赖 (模块二)。apt 的 liburing-dev 不带 CMake config,
# 采用与 hiredis/mysql 同款的 find_path/find_library fallback; 不进 vcpkg.json。
if(BIKE_BUILD_SERVER AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT TARGET uring::uring)
    find_path(LIBURING_INCLUDE_DIR liburing.h)
    find_library(LIBURING_LIB NAMES uring)
    if(NOT LIBURING_INCLUDE_DIR OR NOT LIBURING_LIB)
        message(FATAL_ERROR "liburing not found — install liburing-dev")
    endif()
    add_library(bike_uring_iface INTERFACE)
    target_include_directories(bike_uring_iface INTERFACE ${LIBURING_INCLUDE_DIR})
    target_link_libraries(bike_uring_iface INTERFACE ${LIBURING_LIB})
    add_library(uring::uring ALIAS bike_uring_iface)
endif()

# 顶层开启 ctest, 保证 ctest --test-dir <build> 能发现子目录测试
enable_testing()

add_subdirectory(common)
if(BIKE_BUILD_SERVER)
    add_subdirectory(server)
endif()
if(BIKE_BUILD_CLIENT)
    add_subdirectory(client)
endif()
