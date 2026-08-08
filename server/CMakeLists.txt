find_package(Protobuf REQUIRED)
find_package(spdlog   REQUIRED)

# toml++ is vendored as a single header under third_party/tomlplusplus/
add_library(bike_server_config STATIC src/config.cpp)
target_include_directories(bike_server_config PUBLIC
    include
    ${CMAKE_SOURCE_DIR}/third_party/tomlplusplus
)
if(MSVC)
    # 不依赖 bike_common, 需独立补 /utf-8, 否则中文注释被 CP936 误解析
    target_compile_options(bike_server_config PUBLIC /utf-8)
endif()

add_library(bike_server_logging STATIC src/logging.cpp)
target_include_directories(bike_server_logging PUBLIC include)
target_link_libraries(bike_server_logging PUBLIC spdlog::spdlog)

# Handlers + Router — link against in-memory repos for tests; prod main wires MySQL/Redis
add_library(bike_server_core STATIC
    src/router.cpp
    src/handlers/mobile_code.cpp
    src/handlers/login.cpp
    src/handlers/recharge.cpp
    src/handlers/account_balance.cpp
    src/handlers/list_records.cpp
    src/handlers/list_nearby_bikes.cpp
    src/handlers/scan_unlock.cpp
    src/handlers/position_report.cpp
    src/handlers/end_ride.cpp
    src/handlers/report_damage.cpp
    src/handlers/get_ride_detail.cpp
    src/handlers/list_rides.cpp
    src/auth.cpp
)
target_include_directories(bike_server_core PUBLIC include)
target_link_libraries(bike_server_core PUBLIC
    bike_common
    bike_server_config
    bike_server_logging
)

# ---- Gateway 纯逻辑库 (模块二) ----
# 无 liburing / 无 Linux 系统调用, 跨平台可编译可单测 (Windows 本地回归依赖此层)。
# 切帧(frame_cut) + OutboxQueue 队列 + PacketSink/InProcessRouterSink(头文件)。
add_library(bike_gateway_core STATIC
    src/gateway/frame_cut.cpp
)
target_include_directories(bike_gateway_core PUBLIC include)
target_link_libraries(bike_gateway_core PUBLIC bike_server_core)

# ---- Gateway io_uring 实体 + server 可执行文件: 仅 Linux ----
# Windows 开发机无法编译 io_uring, 部署阶段在云服务器/Docker 内验证。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(bike_server_gateway STATIC
        src/gateway/uring_engine.cpp
        src/gateway/worker_outbox.cpp
    )
    target_include_directories(bike_server_gateway PUBLIC include)
    target_link_libraries(bike_server_gateway PUBLIC
        bike_gateway_core
        uring::uring
    )

    # Server binary
    add_executable(bike-server src/main.cpp)
    target_link_libraries(bike-server PRIVATE
        bike_server_gateway
        bike_server_prod
    )
endif()

# Prod DB + Redis implementations (hiredis::hiredis and MySQL::MySQL come from top-level fallbacks)
# Linux-only: mysql_pool/redis_session_store 依赖 POSIX 头文件(sys/time.h 等),
# Windows 本地无法编译, 与 bike-server 一并留待部署阶段验证。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
add_library(bike_server_prod STATIC
    src/db/mysql_pool.cpp
    src/db/mysql_user_repo.cpp
    src/db/mysql_account_repo.cpp
    src/db/mysql_bike_repo.cpp
    src/db/mysql_ride_repo.cpp
    src/cache/redis_session_store.cpp
)
target_include_directories(bike_server_prod PUBLIC include)
target_link_libraries(bike_server_prod PUBLIC
    bike_server_core
    MySQL::MySQL
    hiredis::hiredis
)
endif()

# Server binary 已移入上方 Linux-only 分支 (bike_server_gateway 块)。

# Tests
enable_testing()
find_package(GTest)
if(GTest_FOUND)
    add_executable(test_config  tests/test_config.cpp)
    target_link_libraries(test_config  PRIVATE bike_server_config GTest::gtest_main)

    add_executable(test_handlers tests/test_handlers.cpp)
    target_link_libraries(test_handlers PRIVATE bike_server_core GTest::gtest_main)

    add_executable(test_ride_session_store tests/test_ride_session_store.cpp)
    target_link_libraries(test_ride_session_store PRIVATE bike_server_core GTest::gtest_main)
    target_include_directories(test_ride_session_store PRIVATE include)

    add_executable(test_handlers_ride tests/test_handlers_ride.cpp)
    target_link_libraries(test_handlers_ride PRIVATE bike_server_core GTest::gtest_main)
    target_include_directories(test_handlers_ride PRIVATE include)

    # Gateway 纯逻辑测试 (跨平台)
    add_executable(test_frame_cut tests/test_frame_cut.cpp)
    target_link_libraries(test_frame_cut PRIVATE bike_gateway_core GTest::gtest_main)

    add_executable(test_outbox_queue tests/test_outbox_queue.cpp)
    target_link_libraries(test_outbox_queue PRIVATE bike_gateway_core GTest::gtest_main)

    add_executable(test_router_sink tests/test_router_sink.cpp)
    target_link_libraries(test_router_sink PRIVATE bike_gateway_core GTest::gtest_main)

    add_test(NAME config  COMMAND test_config)
    add_test(NAME handlers COMMAND test_handlers)
    add_test(NAME ride_session_store COMMAND test_ride_session_store)
    add_test(NAME handlers_ride COMMAND test_handlers_ride)
    add_test(NAME frame_cut      COMMAND test_frame_cut)
    add_test(NAME outbox_queue   COMMAND test_outbox_queue)
    add_test(NAME router_sink    COMMAND test_router_sink)
endif()
