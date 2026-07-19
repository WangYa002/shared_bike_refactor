cmake_minimum_required(VERSION 3.20)
project(bike_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.2 COMPONENTS
    Widgets Concurrent WebEngineWidgets WebChannel Positioning
    REQUIRED)
find_package(Protobuf REQUIRED)

# 协议 + 通用库(由仓库顶层 CMake 提供)
add_executable(bike_client
    src/main.cpp
    src/backend_client.cpp
    src/mainwindow.cpp
    src/trajectory_sim.cpp
    src/location_provider.cpp
    src/map_bridge.cpp
    src/views/login_view.cpp
    src/views/wallet_view.cpp
    src/views/records_view.cpp
    src/views/map_view.cpp
    src/views/ride_view.cpp
    src/views/ride_history_view.cpp
    src/views/ride_detail_dialog.cpp
)
target_link_libraries(bike_client PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::WebEngineWidgets
    Qt6::WebChannel
    Qt6::Positioning
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

# 资源(地图 HTML/JS/CSS)
target_sources(bike_client PRIVATE
    resources/map.qrc
)

# ---- 单元测试 (GTest + Qt Core) ----
find_package(GTest QUIET)
if(GTest_FOUND)
    enable_testing()
    add_executable(bike_client_tests
        tests/test_trajectory_sim.cpp
        src/trajectory_sim.cpp
    )
    target_link_libraries(bike_client_tests PRIVATE
        GTest::gtest
        GTest::gtest_main
        Qt6::Core
        bike_common
    )
    target_include_directories(bike_client_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/common/include
    )
    add_test(NAME bike_client_tests COMMAND bike_client_tests)
endif()
