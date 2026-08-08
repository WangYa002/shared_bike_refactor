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

# ---- 模块三 IPC 纯逻辑库 (跨平台) ----
# IpcPacket/shm 布局/SpscRing/ReplyQueue 均为 header-only 纯逻辑,
# 无系统调用; Windows 单测直接验证无锁协议。
add_library(bike_ipc_core INTERFACE)
target_include_directories(bike_ipc_core INTERFACE include)
if(MSVC)
    # 头文件含中文注释, 消费方需 /utf-8(与 bike_server_config 同理)
    target_compile_options(bike_ipc_core INTERFACE /utf-8)
endif()

# ---- Gateway io_uring 实体 + 双进程可执行文件: 仅 Linux ----
# Windows 开发机无法编译 io_uring/shm, 部署阶段在云服务器/Docker 内验证。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # 模块三 Linux 粘合层: shm/FIFO/RingSink/RingSource
    add_library(bike_ipc STATIC
        src/ipc/shm_region.cpp
        src/ipc/fifo_channel.cpp
        src/ipc/ring_sink.cpp
        src/ipc/ring_source.cpp
    )
    target_include_directories(bike_ipc PUBLIC include)
    target_link_libraries(bike_ipc PUBLIC
        bike_gateway_core      # PacketSink 接口
        bike_ipc_core
        bike_server_logging
        rt                     # shm_open(旧 glibc 兼容)
    )

    add_library(bike_server_gateway STATIC
        src/gateway/uring_engine.cpp
        src/gateway/worker_outbox.cpp
    )
    target_include_directories(bike_server_gateway PUBLIC include)
    target_link_libraries(bike_server_gateway PUBLIC
        bike_gateway_core
        bike_ipc_core
        uring::uring
    )

    # 业务装配库: Ctx 工厂 + 全协议 handler 注册(gateway inprocess 回退 / dispatch 共用)
    add_library(bike_server_app STATIC
        src/app/ctx_factory.cpp
    )
    target_include_directories(bike_server_app PUBLIC include)
    target_link_libraries(bike_server_app PUBLIC
        bike_server_core
        bike_server_prod
    )

    # 纯网关进程(ring 模式下业务路径不进链接闭包外的运行路径;
    # inprocess 回退模式需要 bike_server_app)
    add_executable(bike-gateway src/gateway_main.cpp)
    target_link_libraries(bike-gateway PRIVATE
        bike_server_gateway
        bike_ipc
        bike_server_app
    )

    # 业务 Dispatch 进程(RideSessionStore/全部 repo 归此)
    add_executable(bike-dispatch src/dispatch/dispatch_main.cpp)
    target_link_libraries(bike-dispatch PRIVATE
        bike_server_app
        bike_ipc
    )
endif()

# Prod DB + Redis implementations (hiredis::hiredis and MySQL::MySQL come from top-level fallbacks)
# Linux-only: mysql_pool/redis_session_store 依赖 POSIX 头文件(sys/time.h 等),
# Windows 本地无法编译, 与 bike-gateway/bike-dispatch 一并留待部署阶段验证。
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

# Server binary 已拆为 bike-gateway / bike-dispatch (上方 Linux-only 分支)。

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

    # 模块三 IPC 纯逻辑测试 (跨平台: 堆内存验证无锁协议)
    add_executable(test_ipc_packet tests/test_ipc_packet.cpp)
    target_link_libraries(test_ipc_packet PRIVATE bike_ipc_core GTest::gtest_main)

    add_executable(test_spsc_ring tests/test_spsc_ring.cpp)
    target_link_libraries(test_spsc_ring PRIVATE bike_ipc_core GTest::gtest_main)

    add_test(NAME config  COMMAND test_config)
    add_test(NAME handlers COMMAND test_handlers)
    add_test(NAME ride_session_store COMMAND test_ride_session_store)
    add_test(NAME handlers_ride COMMAND test_handlers_ride)
    add_test(NAME frame_cut      COMMAND test_frame_cut)
    add_test(NAME outbox_queue   COMMAND test_outbox_queue)
    add_test(NAME router_sink    COMMAND test_router_sink)
    add_test(NAME ipc_packet     COMMAND test_ipc_packet)
    add_test(NAME spsc_ring      COMMAND test_spsc_ring)
endif()

# 模块三 Linux 双进程集成测试: fork 真实 shm_open/mmap 验证端到端协议。
# Windows 不可构建不可运行, 仅部署阶段(Docker/Linux)跑。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND GTest_FOUND)
    add_executable(test_ipc_linux tests/test_ipc_linux.cpp)
    target_link_libraries(test_ipc_linux PRIVATE bike_ipc GTest::gtest_main)
    add_test(NAME ipc_linux COMMAND test_ipc_linux)
endif()
