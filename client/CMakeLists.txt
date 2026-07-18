cmake_minimum_required(VERSION 3.20)
project(bike_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 COMPONENTS Widgets Concurrent REQUIRED)
find_package(asio REQUIRED)
find_package(Protobuf REQUIRED)

add_executable(bike_client
    src/main.cpp
    src/backend_client.cpp
    src/mainwindow.cpp
    src/views/login_view.cpp
    src/views/wallet_view.cpp
    src/views/records_view.cpp
)
target_link_libraries(bike_client PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    bike_common
    asio::asio
    protobuf::libprotobuf
)
target_compile_definitions(bike_client PRIVATE
    ASIO_STANDALONE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
)
target_include_directories(bike_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/common/include
)
