cmake_minimum_required(VERSION 3.22)
project(bike_client CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Protobuf REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network)
find_package(GTest QUIET)

# ---- bike_client_core: 与 WebEngine 无关的核心库 ----
# 网络层(TcpWorker/ApiBridge) + 轨迹模拟, 供应用与测试共用;
# 本机若缺 WebEngine 模块, 该库仍可编译, 保证新 C++ 代码的语法级验证与单测可达。
add_library(bike_client_core STATIC
    src/net/tcp_worker.cpp
    src/net/api_bridge.cpp
    src/trajectory_sim.cpp
    src/trajectory_model.cpp
    src/map_bridge.cpp
)
target_link_libraries(bike_client_core PUBLIC
    Qt6::Core
    Qt6::Network
    bike_common
    protobuf::libprotobuf
)
target_include_directories(bike_client_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/common/include
)
if(MSVC)
    target_compile_options(bike_client_core PUBLIC /utf-8)
endif()

# ---- QML 应用 (QML + TcpWorker/ApiBridge 信号槽架构) ----
# 依赖 Quick/WebEngineQuick 等; 缺模块时优雅降级为"只构建 core + 测试"。
find_package(Qt6 6.5 QUIET COMPONENTS
    Quick QuickControls2 WebEngineQuick WebChannel Positioning)

if(Qt6Quick_FOUND AND Qt6QuickControls2_FOUND AND Qt6WebEngineQuick_FOUND
   AND Qt6WebChannel_FOUND AND Qt6Positioning_FOUND)
    qt_add_executable(bike_client
        src/main.cpp
        src/location_provider.cpp
        # 地图资源: qrc:/map.html (WebEngineView 加载)
        resources/map.qrc
    )

    # Theme.qml 是 pragma Singleton。
    # 注意: QML 文件必须与生成的 qmldir 同目录(client/ 根, 即模块基目录);
    # 若放入子目录(如 qml/), 同目录隐式导入看不到 qmldir 的 singleton 条目,
    # Theme 会被当作普通组件实例化导致属性全空 (Qt 官方确认的已知行为)。
    set_source_files_properties(Theme.qml PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE)

    qt_add_qml_module(bike_client
        URI Bike
        VERSION 1.0
        QML_FILES
            Main.qml
            LoginView.qml
            MainScreen.qml
            MapPane.qml
            RidePane.qml
            WalletPane.qml
            RecordsPane.qml
            HistoryPane.qml
            RideDetail.qml
            Theme.qml
            BikeButton.qml
            Field.qml
    )

    target_link_libraries(bike_client PRIVATE
        bike_client_core
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::WebEngineQuick
        Qt6::WebChannel
        Qt6::Positioning
    )
else()
    message(WARNING
        "Qt6 Quick/WebEngineQuick 模块未找到 — 跳过 bike_client 应用构建 "
        "(bike_client_core 与单元测试仍然构建)。安装 Qt WebEngine 后重新配置即可。")
endif()

# ---- 单元测试 (GTest + Qt) ----
if(GTest_FOUND)
    # Qt6 Test 仅测试需要: 移入此块独立 find_package, 无 GTest 时不强制要求。
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    add_executable(bike_client_tests_trajectory
        tests/test_trajectory_sim.cpp
    )
    target_link_libraries(bike_client_tests_trajectory PRIVATE
        bike_client_core
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME client_trajectory COMMAND bike_client_tests_trajectory)

    add_executable(bike_client_tests_tcp
        tests/test_tcp_worker.cpp
    )
    target_link_libraries(bike_client_tests_tcp PRIVATE
        bike_client_core
        GTest::gtest
        Qt6::Test
    )
    add_test(NAME client_tcp_worker COMMAND bike_client_tests_tcp)

    # Windows 下 ctest 运行时需要 Qt 运行库 DLL 在 PATH 中
    set_tests_properties(client_trajectory client_tcp_worker PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        TIMEOUT 60)
endif()
