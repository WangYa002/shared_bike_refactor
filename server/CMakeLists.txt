find_package(Protobuf REQUIRED)
find_package(spdlog   REQUIRED)

# toml++ is vendored as a single header under third_party/tomlplusplus/
add_library(bike_server_config STATIC src/config.cpp)
target_include_directories(bike_server_config PUBLIC
    include
    ${CMAKE_SOURCE_DIR}/third_party/tomlplusplus
)

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
)
target_include_directories(bike_server_core PUBLIC include)
target_link_libraries(bike_server_core PUBLIC
    bike_common
    bike_server_config
    bike_server_logging
)

# Session + Server (asio) — kept in a separate lib so unit tests can skip linking
add_library(bike_server_net STATIC
    src/session.cpp
    src/server.cpp
)
target_include_directories(bike_server_net PUBLIC include)
target_link_libraries(bike_server_net PUBLIC
    bike_server_core
    asio::asio
)
target_compile_definitions(bike_server_net PUBLIC ASIO_STANDALONE)

# Prod DB + Redis implementations (hiredis::hiredis and MySQL::MySQL come from top-level fallbacks)

add_library(bike_server_prod STATIC
    src/db/mysql_pool.cpp
    src/db/mysql_user_repo.cpp
    src/db/mysql_account_repo.cpp
    src/cache/redis_session_store.cpp
)
target_include_directories(bike_server_prod PUBLIC include)
target_link_libraries(bike_server_prod PUBLIC
    bike_server_core
    MySQL::MySQL
    hiredis::hiredis
)

# Server binary
add_executable(bike-server src/main.cpp)
target_link_libraries(bike-server PRIVATE
    bike_server_net
    bike_server_prod
)
target_compile_definitions(bike-server PRIVATE ASIO_STANDALONE)

# Tests
enable_testing()
find_package(GTest)
if(GTest_FOUND)
    add_executable(test_config  tests/test_config.cpp)
    target_link_libraries(test_config  PRIVATE bike_server_config GTest::gtest_main)

    add_executable(test_handlers tests/test_handlers.cpp)
    target_link_libraries(test_handlers PRIVATE bike_server_core GTest::gtest_main)

    add_test(NAME config  COMMAND test_config)
    add_test(NAME handlers COMMAND test_handlers)
endif()
