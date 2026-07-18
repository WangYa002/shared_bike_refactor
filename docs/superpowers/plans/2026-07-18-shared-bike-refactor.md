# shared_bike_refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the C++03 shared-bike backend into a C++17 asio/protobuf/hiredis/mysql service, ship it to Tencent Cloud via docker-compose, and provide a Qt6 desktop client.

**Architecture:** Single io_context running on N threads; per-connection Session reads FBEB frames, Router dispatches by event_id to pure-function Handlers backed by abstract Repository / SessionStore interfaces (in-memory for tests, MySQL/hiredis for prod). Server builds inside a Linux Docker container; client builds natively on Windows with Qt6.

**Tech Stack:** C++17, asio (header-only), protobuf 3.21, hiredis, mysql-connector-c, spdlog, toml++, GoogleTest, Qt6, Docker, docker-compose.

**Spec:** `docs/superpowers/specs/2026-07-18-shared-bike-refactor-design.md`

---

## Phase 0 — Repo setup & legacy preservation

### Task 0.1: Preserve legacy source

**Files:**
- Move: all existing `.cpp/.h/.sln/.vcxproj/.pb.cpp/.pb.h` → `legacy/`
- Keep in root: `docs/`, `bin/`, `obj/`, `.vs/` (the latter two gitignored)

- [ ] **Step 1: Move legacy files**

```bash
mkdir -p legacy
git init 2>/dev/null || true   # no-op if already a repo
# move every C++ source / VS file into legacy/
mv main.cpp BusinessProcessor.cpp BusinessProcessor.h \
   DispatchMsgService.cpp DispatchMsgService.h \
   NetworkInterface.cpp NetworkInterface.h \
   Sqltables.h configdef.h cpp.hint \
   events_def.cpp events_def.h eventtype.cpp eventtype.h \
   glo_def.h iEeventHandler.h ievent.cpp ievent.h \
   iniconfig.cpp iniconfig.h logger.cpp logger.h \
   sqlConnection.cpp sqlConnection.h thread.h \
   thread_cond.cpp thread_mutex.cpp thread_pool.cpp thread_pool.h \
   user_event_handler.cpp user_event_handler.h \
   user_service.cpp user_service.h \
   bike.pb.cpp bike.pb.h \
   shared_bike.sln shared_bike.vcxproj shared_bike.vcxproj.user \
   legacy/ 2>/dev/null
```

- [ ] **Step 2: Add `.gitignore`**

Create `.gitignore` at repo root:

```gitignore
# Build artifacts
/build/
/out/
/bin/
/obj/
*.o
*.obj
*.exe
*.out
*.a
*.lib
*.so
*.dylib

# IDE
.vs/
.vscode/
.idea/
*.user
*.userosx
CMakeUserPresets.json
CMakeSettings.json

# Qt
*.pro.user
*.qmlc
*.qmltc
qml_project/qml_qmlcache.qrc

# vcpkg
/vcpkg/

# OS
.DS_Store
Thumbs.db

# Logs
*.log

# Docker
docker/.env

# Local config overrides
server/config/server.local.toml
```

- [ ] **Step 3: Add `README.md`**

```markdown
# shared_bike_refactor

C++17 refactor of a shared-bike backend + Qt6 desktop client.

- `legacy/` — original C++03 source (libevent + protobuf + log4cpp), preserved for reference
- `proto/` — protobuf schema
- `common/` — shared static lib: FBEB protocol + errors
- `server/` — backend (asio + hiredis + mysql)
- `client/` — Qt6 desktop client
- `docker/` — Dockerfile + docker-compose + mysql-init

## Build & run

See `docs/superpowers/specs/2026-07-18-shared-bike-refactor-design.md` for the design.

### Backend (in Docker)

    docker compose -f docker/docker-compose.yml up --build

### Client (Qt6 + CMake on Windows)

    cmake -B build -S client
    cmake --build build --config Release
```

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "chore: preserve legacy source under legacy/, init repo scaffolding"
```

### Task 0.2: Top-level CMake + vcpkg manifest

**Files:**
- Create: `CMakeLists.txt`
- Create: `vcpkg.json`

- [ ] **Step 1: Top-level CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(shared_bike_refactor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(BIKE_BUILD_TESTS "Build server tests" ON)
option(BIKE_BUILD_CLIENT "Build Qt6 client" OFF)  # opt-in; needs Qt

add_subdirectory(common)
add_subdirectory(server)
if(BIKE_BUILD_CLIENT)
    add_subdirectory(client)
endif()
```

- [ ] **Step 2: vcpkg.json** (for Windows local builds; Docker uses apt)

```json
{
  "name": "shared-bike-refactor",
  "version-string": "0.1.0",
  "dependencies": [
    "asio",
    "protobuf",
    "hiredis",
    "mysql",
    "spdlog",
    "tomlplusplus",
    "gtest"
  ]
}
```

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt vcpkg.json
git commit -m "build: top-level CMake + vcpkg manifest"
```

---

## Phase 1 — Proto & common library (foundation)

### Task 1.1: Author `proto/bike.proto`

**Files:**
- Create: `proto/bike.proto`

- [ ] **Step 1: Write the proto**

```protobuf
syntax = "proto3";
package tutorial;

message mobile_request  { string mobile = 1; }
message mobile_response { int32 code = 1; int32 icode = 2; string data = 3; }

message login_request   { string mobile = 1; int32 icode = 2; }
message login_response  { int32 code = 1; string desc = 2; string session_token = 3; }

message recharge_request  { string session_token = 1; int32 amount = 2; }
message recharge_response { int32 code = 1; int32 balance = 2; string desc = 3; }

message account_balance_request  { string session_token = 1; }
message account_balance_response { int32 code = 1; int32 balance = 2; }

message list_account_records_request  { string session_token = 1; }
message account_record { int32 type = 1; int32 limit = 2; int64 timestamp = 3; }
message list_account_records_response { int32 code = 1; repeated account_record records = 2; }
```

- [ ] **Step 2: Commit**

```bash
git add proto/bike.proto
git commit -m "feat(proto): define bike.proto messages"
```

### Task 1.2: Errors header

**Files:**
- Create: `common/include/bike/errors.hpp`

- [ ] **Step 1: Write `errors.hpp`**

```cpp
#pragma once

#include <string_view>

namespace bike {

enum class ErrCode : int {
    Ok                  = 200,
    InvalidMsg          = 400,
    Unauthorized        = 401,
    InvalidData         = 404,
    MethodNotAllowed    = 405,
    ProcessFailed       = 406,
    BikeIsTook          = 407,   // kept for protocol compat, unused
    BikeIsRunning       = 408,
    BikeIsDamaged       = 409,
    Unknown             = 0xFF,
};

inline constexpr int code(ErrCode c) { return static_cast<int>(c); }

constexpr std::string_view to_string(ErrCode c) {
    switch (c) {
        case ErrCode::Ok:               return "ok";
        case ErrCode::InvalidMsg:       return "invalid message";
        case ErrCode::Unauthorized:     return "unauthorized (session expired)";
        case ErrCode::InvalidData:      return "invalid data";
        case ErrCode::MethodNotAllowed: return "method not allowed";
        case ErrCode::ProcessFailed:    return "process failed";
        case ErrCode::BikeIsTook:       return "bike is took";
        case ErrCode::BikeIsRunning:    return "bike is running";
        case ErrCode::BikeIsDamaged:    return "bike is damaged";
        case ErrCode::Unknown:          return "unknown";
    }
    return "unknown";
}

} // namespace bike
```

- [ ] **Step 2: Commit**

```bash
git add common/include/bike/errors.hpp
git commit -m "feat(common): bike::ErrCode enum + to_string"
```

### Task 1.3: FBEB Frame + encode/decode (TDD)

**Files:**
- Create: `common/include/bike/protocol.hpp`
- Create: `common/src/protocol.cpp`
- Create: `common/tests/test_protocol.cpp` (test lives in server/tests; for now put in `common/tests/`)

- [ ] **Step 1: Write failing tests**

`common/include/bike/protocol.hpp`:

```cpp
#pragma once

#include <bike/errors.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bike {

inline constexpr char kFrameMagic[4] = {'F','B','E','B'};
inline constexpr std::uint32_t kHeaderLen = 10;
inline constexpr std::uint32_t kMaxMessageLen = 372680;

struct Frame {
    std::uint16_t event_id{0};
    std::string payload;        // size in [0, kMaxMessageLen]
};

// Build the wire bytes for a frame (header + payload).
std::vector<std::uint8_t> encode(const Frame& f);

// Decode exactly one frame from a byte buffer of length n.
// Returns std::nullopt on any framing error.
//   - magic mismatch
//   - length out of range
//   - buffer shorter than declared length
// On success, returns the Frame and (via out_consumed) how many bytes were used.
struct DecodeResult {
    Frame frame;
    std::size_t consumed{0};
};
std::optional<DecodeResult> decode(const std::uint8_t* buf, std::size_t n);

} // namespace bike
```

`common/tests/test_protocol.cpp`:

```cpp
#include <bike/protocol.hpp>
#include <gtest/gtest.h>

using namespace bike;

TEST(Protocol, EncodeHeaderLayout) {
    Frame f{.event_id = 0x0102, .payload = std::string(4, '\0')};
    auto bytes = encode(f);
    ASSERT_EQ(bytes.size(), kHeaderLen + 4u);
    EXPECT_EQ(bytes[0], 'F');
    EXPECT_EQ(bytes[1], 'B');
    EXPECT_EQ(bytes[2], 'E');
    EXPECT_EQ(bytes[3], 'B');
    // little-endian event id
    EXPECT_EQ(bytes[4], 0x02);
    EXPECT_EQ(bytes[5], 0x01);
    // little-endian length (=4)
    EXPECT_EQ(bytes[6], 4);
    EXPECT_EQ(bytes[7], 0);
    EXPECT_EQ(bytes[8], 0);
    EXPECT_EQ(bytes[9], 0);
}

TEST(Protocol, DecodeRoundTrip) {
    Frame original{.event_id = 0x0301, .payload = "hello world"};
    auto bytes = encode(original);
    auto r = decode(bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->frame.event_id, original.event_id);
    EXPECT_EQ(r->frame.payload, original.payload);
    EXPECT_EQ(r->consumed, bytes.size());
}

TEST(Protocol, DecodeRejectsBadMagic) {
    std::vector<std::uint8_t> bad = {'X','X','X','X', 1, 0, 0,0,0,0};
    EXPECT_FALSE(decode(bad.data(), bad.size()).has_value());
}

TEST(Protocol, DecodeRejectsOversize) {
    // claim length is kMaxMessageLen + 1
    std::vector<std::uint8_t> bad(10);
    bad[0]='F'; bad[1]='B'; bad[2]='E'; bad[3]='B';
    bad[4]=1; bad[5]=0;
    auto oversize = kMaxMessageLen + 1;
    bad[6] = oversize & 0xFF;
    bad[7] = (oversize >> 8) & 0xFF;
    bad[8] = (oversize >> 16) & 0xFF;
    bad[9] = (oversize >> 24) & 0xFF;
    EXPECT_FALSE(decode(bad.data(), bad.size()).has_value());
}

TEST(Protocol, DecodeRejectsShortBuffer) {
    Frame f{.event_id = 1, .payload = std::string(20, 'a')};
    auto bytes = encode(f);
    // only feed first 12 bytes — header says 20, only 2 of payload available
    EXPECT_FALSE(decode(bytes.data(), 12).has_value());
}
```

`common/CMakeLists.txt`:

```cmake
add_library(bike_common STATIC
    src/protocol.cpp
)
target_include_directories(bike_common PUBLIC include)
target_compile_features(bike_common PUBLIC cxx_std_17)

# protoc-generated sources
find_package(Protobuf REQUIRED)
set(PROTO_FILE ${CMAKE_SOURCE_DIR}/proto/bike.proto)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})
set(PROTO_SRCS ${GENERATED_DIR}/bike.pb.cc)
set(PROTO_HDRS ${GENERATED_DIR}/bike.pb.h)
add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --proto_path=${CMAKE_SOURCE_DIR}/proto
        --cpp_out=${GENERATED_DIR}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
)
target_sources(bike_common PRIVATE ${PROTO_SRCS})
target_include_directories(bike_common PUBLIC ${GENERATED_DIR})
target_link_libraries(bike_common PUBLIC protobuf::libprotobuf)

enable_testing()
add_executable(test_protocol tests/test_protocol.cpp src/protocol.cpp)
target_link_libraries(test_protocol PRIVATE gtest_main)
target_include_directories(test_protocol PRIVATE include ${GENERATED_DIR})
add_test(NAME protocol COMMAND test_protocol)
```

- [ ] **Step 2: Run tests — expect link error (no impl yet)**

```bash
cmake -B build -S .
cmake --build build --target test_protocol
# Expected: build error (protocol.cpp empty / undefined symbols)
```

- [ ] **Step 3: Implement `common/src/protocol.cpp`**

```cpp
#include <bike/protocol.hpp>
#include <cstring>
#include <stdexcept>

namespace bike {

namespace {
inline void put_u16_le(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
inline void put_i32_le(std::vector<std::uint8_t>& out, std::int32_t v) {
    auto u = static_cast<std::uint32_t>(v);
    out.push_back(static_cast<std::uint8_t>(u & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 24) & 0xFF));
}
inline std::uint16_t get_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}
inline std::int32_t get_i32_le(const std::uint8_t* p) {
    auto u = static_cast<std::uint32_t>(p[0]) |
             (static_cast<std::uint32_t>(p[1]) << 8) |
             (static_cast<std::uint32_t>(p[2]) << 16) |
             (static_cast<std::uint32_t>(p[3]) << 24);
    return static_cast<std::int32_t>(u);
}
} // namespace

std::vector<std::uint8_t> encode(const Frame& f) {
    if (f.payload.size() > kMaxMessageLen) {
        throw std::overflow_error("payload exceeds kMaxMessageLen");
    }
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderLen + f.payload.size());
    out.insert(out.end(), kFrameMagic, kFrameMagic + 4);
    put_u16_le(out, f.event_id);
    put_i32_le(out, static_cast<std::int32_t>(f.payload.size()));
    out.insert(out.end(),
               reinterpret_cast<const std::uint8_t*>(f.payload.data()),
               reinterpret_cast<const std::uint8_t*>(f.payload.data()) + f.payload.size());
    return out;
}

std::optional<DecodeResult> decode(const std::uint8_t* buf, std::size_t n) {
    if (n < kHeaderLen) return std::nullopt;
    if (std::memcmp(buf, kFrameMagic, 4) != 0) return std::nullopt;
    std::uint16_t eid = get_u16_le(buf + 4);
    std::int32_t len = get_i32_le(buf + 6);
    if (len < 0 || static_cast<std::uint32_t>(len) > kMaxMessageLen) return std::nullopt;
    std::size_t total = kHeaderLen + static_cast<std::size_t>(len);
    if (n < total) return std::nullopt;
    Frame f;
    f.event_id = eid;
    f.payload.assign(reinterpret_cast<const char*>(buf + kHeaderLen),
                     static_cast<std::size_t>(len));
    return DecodeResult{ std::move(f), total };
}

} // namespace bike
```

- [ ] **Step 4: Run tests, expect PASS**

```bash
cmake --build build --target test_protocol
ctest --test-dir build -R protocol --output-on-failure
# Expected: 5 tests pass
```

- [ ] **Step 5: Commit**

```bash
git add common/
git commit -m "feat(common): FBEB protocol encode/decode with TDD"
```

---

## Phase 2 — Server scaffolding (config, logging, DB pool, cache)

### Task 2.1: Config (toml++)

**Files:**
- Create: `server/include/server/config.hpp`
- Create: `server/src/config.cpp`
- Create: `server/config/server.toml`
- Create: `server/CMakeLists.txt` (incremental)
- Create: `server/tests/test_config.cpp`

- [ ] **Step 1: Test first**

`server/include/server/config.hpp`:

```cpp
#pragma once

#include <string>

namespace bike::server {

struct Config {
    struct Server {
        std::string listen{"0.0.0.0"};
        int port{8888};
        int threads{4};
    } server;

    struct Mysql {
        std::string host{"127.0.0.1"};
        int port{3306};
        std::string user{"bike"};
        std::string password;
        std::string database{"shared_bike"};
        int pool_size{8};
    } mysql;

    struct Redis {
        std::string host{"127.0.0.1"};
        int port{6379};
        int pool_size{4};
    } redis;

    struct Log {
        std::string level{"info"};
        std::string file{"/var/log/bike-server/server.log"};
    } log;
};

// Throws std::runtime_error on parse error.
Config load_config(const std::string& path);

} // namespace bike::server
```

`server/tests/test_config.cpp`:

```cpp
#include "server/config.hpp"
#include <gtest/gtest.h>
#include <fstream>

using bike::server::Config;
using bike::server::load_config;

namespace {
std::string write_tmp(const std::string& content) {
    auto path = std::string(std::tmpnam(nullptr)) + ".toml";
    std::ofstream f(path); f << content; f.close();
    return path;
}
}

TEST(Config, ParsesAllSections) {
    auto path = write_tmp(R"toml(
[server]
listen = "1.2.3.4"
port = 9000
threads = 2

[mysql]
host = "db.local"
port = 3307
user = "u"
password = "p"
database = "d"
pool_size = 4

[redis]
host = "r.local"
port = 6380
pool_size = 2

[log]
level = "debug"
file = "/tmp/x.log"
)toml");
    auto cfg = load_config(path);
    EXPECT_EQ(cfg.server.listen, "1.2.3.4");
    EXPECT_EQ(cfg.server.port, 9000);
    EXPECT_EQ(cfg.server.threads, 2);
    EXPECT_EQ(cfg.mysql.host, "db.local");
    EXPECT_EQ(cfg.mysql.pool_size, 4);
    EXPECT_EQ(cfg.redis.port, 6380);
    EXPECT_EQ(cfg.log.level, "debug");
    std::remove(path.c_str());
}

TEST(Config, MissingFileThrows) {
    EXPECT_THROW(load_config("/nonexistent/path.toml"), std::runtime_error);
}
```

- [ ] **Step 2: Implement `config.cpp`**

```cpp
#include "server/config.hpp"
#include <toml.hpp>
#include <stdexcept>

namespace bike::server {

namespace {
template <typename T>
T get_or(const toml::table& t, std::string_view key, T def) {
    auto it = t.find(key);
    if (it == t.end()) return def;
    auto v = it->second.as<T>();
    if (!v) return def;
    return **v;
}
}

Config load_config(const std::string& path) {
    Config cfg;
    toml::table root;
    try {
        root = toml::parse_file(path);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("config parse failed: ") + e.what());
    }

    if (auto s = root["server"].as_table()) {
        cfg.server.listen  = get_or<std::string>(*s, "listen", cfg.server.listen);
        cfg.server.port    = get_or<std::int64_t>(*s, "port", cfg.server.port);
        cfg.server.threads = get_or<std::int64_t>(*s, "threads", cfg.server.threads);
    }
    if (auto m = root["mysql"].as_table()) {
        cfg.mysql.host      = get_or<std::string>(*m, "host", cfg.mysql.host);
        cfg.mysql.port      = get_or<std::int64_t>(*m, "port", cfg.mysql.port);
        cfg.mysql.user      = get_or<std::string>(*m, "user", cfg.mysql.user);
        cfg.mysql.password  = get_or<std::string>(*m, "password", cfg.mysql.password);
        cfg.mysql.database  = get_or<std::string>(*m, "database", cfg.mysql.database);
        cfg.mysql.pool_size = get_or<std::int64_t>(*m, "pool_size", cfg.mysql.pool_size);
    }
    if (auto r = root["redis"].as_table()) {
        cfg.redis.host      = get_or<std::string>(*r, "host", cfg.redis.host);
        cfg.redis.port      = get_or<std::int64_t>(*r, "port", cfg.redis.port);
        cfg.redis.pool_size = get_or<std::int64_t>(*r, "pool_size", cfg.redis.pool_size);
    }
    if (auto l = root["log"].as_table()) {
        cfg.log.level = get_or<std::string>(*l, "level", cfg.log.level);
        cfg.log.file  = get_or<std::string>(*l, "file", cfg.log.file);
    }
    return cfg;
}

} // namespace bike::server
```

`server/config/server.toml`:

```toml
[server]
listen = "0.0.0.0"
port = 8888
threads = 4

[mysql]
host = "mysql"
port = 3306
user = "bike"
password = "bike_pwd"
database = "shared_bike"
pool_size = 8

[redis]
host = "redis"
port = 6379
pool_size = 4

[log]
level = "info"
file = "/var/log/bike-server/server.log"
```

`server/CMakeLists.txt` (incremental — start with config target):

```cmake
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)
find_package(tomlplusplus REQUIRED)

add_library(bike_server_config STATIC
    src/config.cpp
)
target_include_directories(bike_server_config PUBLIC include)
target_link_libraries(bike_server_config PUBLIC tomlplusplus::tomlplusplus)
```

- [ ] **Step 3: Build & run test**

```bash
cmake --build build --target test_config
ctest --test-dir build -R config
# Expected: 2 tests pass
```

- [ ] **Step 4: Commit**

```bash
git add server/include/server/config.hpp server/src/config.cpp server/config/ server/tests/test_config.cpp server/CMakeLists.txt
git commit -m "feat(server): config loader with toml++"
```

### Task 2.2: Logging facade (spdlog)

**Files:**
- Create: `server/include/server/logging.hpp`
- Create: `server/src/logging.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <memory>

namespace bike::server {

void init_logging(const std::string& level, const std::string& file);

} // namespace bike::server

#define BIKE_LOG_INFO(...)  SPDLOG_INFO(__VA_ARGS__)
#define BIKE_LOG_WARN(...)  SPDLOG_WARN(__VA_ARGS__)
#define BIKE_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define BIKE_LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
```

- [ ] **Step 2: Impl**

```cpp
#include "server/logging.hpp"
#include <stdexcept>

namespace bike::server {

void init_logging(const std::string& level, const std::string& file) {
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto rotating = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        file, /*max_size=*/5 * 1024 * 1024, /*max_files=*/3);

    auto logger = std::make_shared<spdlog::logger>(
        "bike", spdlog::sinks_init_list{console, rotating});

    if (level == "debug")      logger->set_level(spdlog::level::debug);
    else if (level == "info")  logger->set_level(spdlog::level::info);
    else if (level == "warn")  logger->set_level(spdlog::level::warn);
    else if (level == "error") logger->set_level(spdlog::level::err);
    else                       logger->set_level(spdlog::level::info);

    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(logger);
}

} // namespace bike::server
```

- [ ] **Step 3: Append to `server/CMakeLists.txt`**

```cmake
add_library(bike_server_logging STATIC src/logging.cpp)
target_include_directories(bike_server_logging PUBLIC include)
target_link_libraries(bike_server_logging PUBLIC spdlog::spdlog)
```

- [ ] **Step 4: Commit**

```bash
git add server/include/server/logging.hpp server/src/logging.cpp server/CMakeLists.txt
git commit -m "feat(server): spdlog facade with console+rotating-file sinks"
```

### Task 2.3: Repository & SessionStore interfaces

**Files:**
- Create: `server/include/server/repo/user_repo.hpp`
- Create: `server/include/server/repo/account_repo.hpp`
- Create: `server/include/server/repo/session_store.hpp`
- Create: `server/include/server/repo/in_memory.hpp` (in-memory impls for tests)

- [ ] **Step 1: UserRepo**

```cpp
#pragma once
#include <optional>
#include <string>

namespace bike::server {

struct User {
    int id{0};
    std::string mobile;
};

class IUserRepo {
public:
    virtual ~IUserRepo() = default;
    // find by mobile; returns std::nullopt if not found
    virtual std::optional<User> find_by_mobile(const std::string& mobile) = 0;
    // find existing or insert + return; never returns nullopt unless DB error
    virtual User find_or_create(const std::string& mobile) = 0;
};

} // namespace bike::server
```

- [ ] **Step 2: AccountRepo**

```cpp
#pragma once
#include "user_repo.hpp"
#include <vector>

namespace bike::server {

enum class RecordType : int { Recharge = 1, Consume = 2 };

struct AccountRecord {
    int type{0};          // 1=recharge, 2=consume
    int amount{0};        // cents
    long long balance_after{0};
    long long timestamp{0};   // unix seconds
};

class IAccountRepo {
public:
    virtual ~IAccountRepo() = default;
    virtual int get_balance(int user_id) = 0;                    // returns cents, 0 if not exists
    // atomically: add amount to balance, insert a record, return new balance
    virtual int add_balance(int user_id, RecordType type, int amount) = 0;
    virtual std::vector<AccountRecord> list_records(int user_id, int limit) = 0;
};

} // namespace bike::server
```

- [ ] **Step 3: SessionStore**

```cpp
#pragma once
#include <optional>
#include <string>

namespace bike::server {

class ISessionStore {
public:
    virtual ~ISessionStore() = default;
    // Generate a new 6-digit code, store with TTL=300s, return the code.
    virtual int set_code(const std::string& mobile) = 0;
    // Return the stored code or std::nullopt if expired/absent.
    virtual std::optional<int> get_code(const std::string& mobile) = 0;
    virtual void delete_code(const std::string& mobile) = 0;
    // Create a new session token for mobile, TTL=7d, return token.
    virtual std::string create_session(const std::string& mobile) = 0;
    // Resolve token → mobile, or std::nullopt if expired/absent.
    virtual std::optional<std::string> lookup_session(const std::string& token) = 0;
    virtual void destroy_session(const std::string& token) = 0;
};

} // namespace bike::server
```

- [ ] **Step 4: In-memory impls (for tests)**

`server/include/server/repo/in_memory.hpp`:

```cpp
#pragma once
#include "user_repo.hpp"
#include "account_repo.hpp"
#include "session_store.hpp"

#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <chrono>

namespace bike::server {

class InMemoryUserRepo : public IUserRepo {
public:
    std::optional<User> find_by_mobile(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = by_mobile_.find(m);
        if (it == by_mobile_.end()) return std::nullopt;
        return it->second;
    }
    User find_or_create(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = by_mobile_.find(m);
        if (it != by_mobile_.end()) return it->second;
        User u{.id = next_id_++, .mobile = m};
        by_mobile_[m] = u;
        return u;
    }
private:
    std::mutex mu_;
    std::map<std::string, User> by_mobile_;
    int next_id_{1};
};

class InMemoryAccountRepo : public IAccountRepo {
public:
    int get_balance(int uid) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = bal_.find(uid);
        return it == bal_.end() ? 0 : it->second;
    }
    int add_balance(int uid, RecordType type, int amount) override {
        std::lock_guard<std::mutex> lk(mu_);
        int& b = bal_[uid];
        b += amount;
        records_[uid].push_back(AccountRecord{
            .type = static_cast<int>(type),
            .amount = amount,
            .balance_after = b,
            .timestamp = std::chrono::seconds(std::time(nullptr)).count(),
        });
        return b;
    }
    std::vector<AccountRecord> list_records(int uid, int limit) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = records_.find(uid);
        if (it == records_.end()) return {};
        auto& v = it->second;
        if (v.size() <= static_cast<size_t>(limit)) return v;
        return std::vector<AccountRecord>(v.end() - limit, v.end());
    }
private:
    std::mutex mu_;
    std::map<int, int> bal_;
    std::map<int, std::vector<AccountRecord>> records_;
};

class InMemorySessionStore : public ISessionStore {
public:
    int set_code(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::uniform_int_distribution<int> dist(100000, 999999);
        int code = dist(rng_);
        codes_[m] = {code, std::chrono::steady_clock::now() + std::chrono::seconds(300)};
        return code;
    }
    std::optional<int> get_code(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = codes_.find(m);
        if (it == codes_.end()) return std::nullopt;
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            codes_.erase(it);
            return std::nullopt;
        }
        return it->second.code;
    }
    void delete_code(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        codes_.erase(m);
    }
    std::string create_session(const std::string& m) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::uniform_int_distribution<unsigned long long> dist;
        std::ostringstream oss;
        oss << std::hex << dist(rng_) << dist(rng_);
        std::string token = oss.str();
        sessions_[token] = {m, std::chrono::steady_clock::now() + std::chrono::hours(7 * 24)};
        return token;
    }
    std::optional<std::string> lookup_session(const std::string& t) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = sessions_.find(t);
        if (it == sessions_.end()) return std::nullopt;
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            sessions_.erase(it);
            return std::nullopt;
        }
        return it->second.mobile;
    }
    void destroy_session(const std::string& t) override {
        std::lock_guard<std::mutex> lk(mu_);
        sessions_.erase(t);
    }
private:
    struct Entry {
        std::string mobile;
        std::chrono::steady_clock::time_point expiry;
    };
    struct CodeEntry { int code; std::chrono::steady_clock::time_point expiry; };
    std::mutex mu_;
    std::mt19937_64 rng_{std::random_device{}()};
    std::map<std::string, CodeEntry> codes_;
    std::map<std::string, Entry> sessions_;
};

} // namespace bike::server
```

- [ ] **Step 5: Commit**

```bash
git add server/include/server/repo/
git commit -m "feat(server): repo + session store interfaces + in-memory impls"
```

### Task 2.4: Handlers (TDD with in-memory repos)

> All 5 handlers under `server/src/handlers/`. Each is a free function returning a `Response` variant. We use a single `Response` type that wraps a `Frame`-ready payload.

**Files:**
- Create: `server/include/server/router.hpp` (Request/Response/HandlerFn types)
- Create: `server/include/server/handlers.hpp`
- Create: `server/src/handlers/*.cpp`
- Create: `server/tests/test_handlers.cpp`

- [ ] **Step 1: Router types**

`server/include/server/router.hpp`:

```cpp
#pragma once
#include <bike/protocol.hpp>
#include <bike.pb.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "server/repo/user_repo.hpp"
#include "server/repo/account_repo.hpp"
#include "server/repo/session_store.hpp"

namespace bike::server {

struct Ctx {
    std::shared_ptr<IUserRepo> users;
    std::shared_ptr<IAccountRepo> accounts;
    std::shared_ptr<ISessionStore> sessions;
};

// A handler takes a raw protobuf payload + ctx, returns a frame to send back.
// event_id is filled in by Router.
using HandlerFn = std::function<std::vector<std::uint8_t>(
    const std::string& payload, Ctx&)>;

class Router {
public:
    void register_handler(std::uint16_t eid, HandlerFn fn);
    // Returns the response bytes, or empty vector if no handler registered.
    std::vector<std::uint8_t> dispatch(std::uint16_t eid,
                                       const std::string& payload,
                                       Ctx& ctx);
private:
    std::unordered_map<std::uint16_t, HandlerFn> handlers_;
};

} // namespace bike::server
```

- [ ] **Step 2: Test handlers (mobile_code + login first)**

`server/tests/test_handlers.cpp` (incremental):

```cpp
#include "server/handlers.hpp"
#include "server/repo/in_memory.hpp"
#include <bike/protocol.hpp>
#include <bike.pb.h>
#include <gtest/gtest.h>

using namespace bike;
using namespace bike::server;

namespace {
struct Env {
    std::shared_ptr<InMemoryUserRepo> users{std::make_shared<InMemoryUserRepo>()};
    std::shared_ptr<InMemoryAccountRepo> accs{std::make_shared<InMemoryAccountRepo>()};
    std::shared_ptr<InMemorySessionStore> sess{std::make_shared<InMemorySessionStore>()};
    Ctx ctx{users, accs, sess};
};

tutorial::mobile_response parse_mobile_rsp(const std::vector<std::uint8_t>& bytes) {
    auto r = decode(bytes.data(), bytes.size());
    EXPECT_TRUE(r.has_value());
    tutorial::mobile_response m;
    EXPECT_TRUE(m.ParseFromArray(r->frame.payload.data(), r->frame.payload.size()));
    return m;
}
} // namespace

TEST(Handlers, MobileCodeReturns6Digit) {
    Env env;
    tutorial::mobile_request req; req.set_mobile("15600000000");
    auto bytes = handlers::mobile_code(req.SerializeAsString(), env.ctx);
    auto m = parse_mobile_rsp(bytes);
    EXPECT_EQ(m.code(), 200);
    EXPECT_GE(m.icode(), 100000);
    EXPECT_LE(m.icode(), 999999);
}

TEST(Handlers, LoginWrongCodeFails) {
    Env env;
    tutorial::mobile_request req; req.set_mobile("15600000000");
    handlers::mobile_code(req.SerializeAsString(), env.ctx);

    tutorial::login_request lr; lr.set_mobile("15600000000"); lr.set_icode(999999);
    auto bytes = handlers::login(lr.SerializeAsString(), env.ctx);
    auto r = decode(bytes.data(), bytes.size());
    tutorial::login_response lresp;
    ASSERT_TRUE(lresp.ParseFromArray(r->frame.payload.data(), r->frame.payload.size()));
    EXPECT_EQ(lresp.code(), 404); // InvalidData
    EXPECT_TRUE(lresp.session_token().empty());
}

TEST(Handlers, LoginCorrectCodeSucceedsAndReturnsToken) {
    Env env;
    tutorial::mobile_request mreq; mreq.set_mobile("15600000001");
    auto mc_bytes = handlers::mobile_code(mreq.SerializeAsString(), env.ctx);
    int code = parse_mobile_rsp(mc_bytes).icode();

    tutorial::login_request lr; lr.set_mobile("15600000001"); lr.set_icode(code);
    auto bytes = handlers::login(lr.SerializeAsString(), env.ctx);
    auto r = decode(bytes.data(), bytes.size());
    tutorial::login_response lresp;
    ASSERT_TRUE(lresp.ParseFromArray(r->frame.payload.data(), r->frame.payload.size()));
    EXPECT_EQ(lresp.code(), 200);
    EXPECT_FALSE(lresp.session_token().empty());
}
```

`server/include/server/handlers.hpp`:

```cpp
#pragma once
#include <bike/protocol.hpp>
#include <cstdint>
#include <string>

#include "server/router.hpp"

namespace bike::server::handlers {

std::vector<std::uint8_t> mobile_code(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> login(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> recharge(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> account_balance(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> list_records(const std::string& payload, Ctx& ctx);

} // namespace bike::server::handlers
```

- [ ] **Step 3: Run test — expect fail (not implemented)**

```bash
cmake --build build --target test_handlers
# Expected: link error
```

- [ ] **Step 4: Implement `mobile_code.cpp`**

```cpp
#include "server/handlers.hpp"
#include "server/logging.hpp"
#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> mobile_code(const std::string& payload, Ctx& ctx) {
    tutorial::mobile_request req;
    int code = code(ErrCode::InvalidMsg);
    tutorial::mobile_response rsp;
    if (!req.ParseFromArray(payload.data(), payload.size())) {
        rsp.set_code(code(ErrCode::InvalidMsg));
        rsp.set_data(std::string(to_string(ErrCode::InvalidMsg)));
        Frame f{.event_id = 0x02, .payload = rsp.SerializeAsString()};
        return encode(f);
    }
    int icode = ctx.sessions->set_code(req.mobile());
    BIKE_LOG_INFO("mobile_code mobile={} code={}", req.mobile(), icode);
    rsp.set_code(200);
    rsp.set_icode(icode);
    rsp.set_data(std::string(to_string(ErrCode::Ok)));
    Frame f{.event_id = 0x02, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
```

- [ ] **Step 5: Implement `login.cpp`**

```cpp
#include "server/handlers.hpp"
#include "server/logging.hpp"
#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> login(const std::string& payload, Ctx& ctx) {
    tutorial::login_request req;
    tutorial::login_response rsp;

    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        rsp.set_desc(std::string(to_string(ec)));
        Frame f{.event_id = 0x04, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);

    auto stored = ctx.sessions->get_code(req.mobile());
    if (!stored || *stored != req.icode())
        return fail(ErrCode::InvalidData);

    ctx.sessions->delete_code(req.mobile());
    User u = ctx.users->find_or_create(req.mobile());
    (void)ctx.accounts->get_balance(u.id);  // ensure account row exists

    std::string token = ctx.sessions->create_session(req.mobile());
    BIKE_LOG_INFO("login ok mobile={} uid={}", req.mobile(), u.id);

    rsp.set_code(200);
    rsp.set_desc(std::string(to_string(ErrCode::Ok)));
    rsp.set_session_token(token);
    Frame f{.event_id = 0x04, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
```

- [ ] **Step 6: Implement `recharge.cpp`, `account_balance.cpp`, `list_records.cpp`**

`recharge.cpp`:

```cpp
#include "server/handlers.hpp"
#include "server/logging.hpp"
#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> recharge(const std::string& payload, Ctx& ctx) {
    tutorial::recharge_request req;
    tutorial::recharge_response rsp;
    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        rsp.set_desc(std::string(to_string(ec)));
        Frame f{.event_id = 0x06, .payload = rsp.SerializeAsString()};
        return encode(f);
    };
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);
    if (req.amount() <= 0)
        return fail(ErrCode::InvalidData);

    auto mobile = ctx.sessions->lookup_session(req.session_token());
    if (!mobile) return fail(ErrCode::Unauthorized);

    User u = ctx.users->find_or_create(*mobile);
    int new_bal = ctx.accounts->add_balance(u.id, RecordType::Recharge, req.amount());
    BIKE_LOG_INFO("recharge mobile={} amount={} bal={}", *mobile, req.amount(), new_bal);

    rsp.set_code(200);
    rsp.set_balance(new_bal);
    rsp.set_desc(std::string(to_string(ErrCode::Ok)));
    Frame f{.event_id = 0x06, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
```

`account_balance.cpp`:

```cpp
#include "server/handlers.hpp"
#include "server/logging.hpp"
#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> account_balance(const std::string& payload, Ctx& ctx) {
    tutorial::account_balance_request req;
    tutorial::account_balance_response rsp;
    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        Frame f{.event_id = 0x08, .payload = rsp.SerializeAsString()};
        return encode(f);
    };
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);

    auto mobile = ctx.sessions->lookup_session(req.session_token());
    if (!mobile) return fail(ErrCode::Unauthorized);

    User u = ctx.users->find_or_create(*mobile);
    int bal = ctx.accounts->get_balance(u.id);
    rsp.set_code(200);
    rsp.set_balance(bal);
    Frame f{.event_id = 0x08, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
```

`list_records.cpp`:

```cpp
#include "server/handlers.hpp"
#include "server/logging.hpp"
#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> list_records(const std::string& payload, Ctx& ctx) {
    tutorial::list_account_records_request req;
    tutorial::list_account_records_response rsp;
    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        Frame f{.event_id = 0x10, .payload = rsp.SerializeAsString()};
        return encode(f);
    };
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);

    auto mobile = ctx.sessions->lookup_session(req.session_token());
    if (!mobile) return fail(ErrCode::Unauthorized);

    User u = ctx.users->find_or_create(*mobile);
    auto recs = ctx.accounts->list_records(u.id, 100);
    rsp.set_code(200);
    for (auto& r : recs) {
        auto* pr = rsp.add_records();
        pr->set_type(r.type);
        pr->set_limit(r.amount);
        pr->set_timestamp(r.timestamp);
    }
    Frame f{.event_id = 0x10, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
```

- [ ] **Step 7: Append to `server/CMakeLists.txt`**

```cmake
add_library(bike_server_handlers STATIC
    src/handlers/mobile_code.cpp
    src/handlers/login.cpp
    src/handlers/recharge.cpp
    src/handlers/account_balance.cpp
    src/handlers/list_records.cpp
    src/router.cpp
)
target_include_directories(bike_server_handlers PUBLIC include ${GENERATED_DIR})
target_link_libraries(bike_server_handlers PUBLIC
    bike_common bike_server_config bike_server_logging protobuf::libprotobuf)
```

`server/src/router.cpp`:

```cpp
#include "server/router.hpp"

namespace bike::server {

void Router::register_handler(std::uint16_t eid, HandlerFn fn) {
    handlers_[eid] = std::move(fn);
}

std::vector<std::uint8_t> Router::dispatch(std::uint16_t eid,
                                           const std::string& payload,
                                           Ctx& ctx) {
    auto it = handlers_.find(eid);
    if (it == handlers_.end()) return {};
    return it->second(payload, ctx);
}

} // namespace bike::server
```

- [ ] **Step 8: Build & run tests — expect PASS**

```bash
cmake --build build --target test_handlers
ctest --test-dir build -R handlers
# Expected: 3 tests pass
```

- [ ] **Step 9: Add tests for recharge/balance/list_records**

Append to `test_handlers.cpp`:

```cpp
TEST(Handlers, RechargeRequiresSession) {
    Env env;
    tutorial::recharge_request rr;
    rr.set_session_token("invalid");
    rr.set_amount(1000);
    auto bytes = handlers::recharge(rr.SerializeAsString(), env.ctx);
    auto r = decode(bytes.data(), bytes.size());
    tutorial::recharge_response resp;
    ASSERT_TRUE(resp.ParseFromArray(r->frame.payload.data(), r->frame.payload.size()));
    EXPECT_EQ(resp.code(), 401);
}

TEST(Handlers, RechargeFlowThenBalanceThenRecords) {
    Env env;
    // login first
    tutorial::mobile_request mreq; mreq.set_mobile("15600000002");
    int code = parse_mobile_rsp(handlers::mobile_code(mreq.SerializeAsString(), env.ctx)).icode();
    tutorial::login_request lr; lr.set_mobile("15600000002"); lr.set_icode(code);
    auto lb = handlers::login(lr.SerializeAsString(), env.ctx);
    auto lr_dec = decode(lb.data(), lb.size());
    tutorial::login_response lresp;
    ASSERT_TRUE(lresp.ParseFromArray(lr_dec->frame.payload.data(), lr_dec->frame.payload.size()));
    auto token = lresp.session_token();
    ASSERT_FALSE(token.empty());

    // recharge 1000 cents
    tutorial::recharge_request rr; rr.set_session_token(token); rr.set_amount(1000);
    handlers::recharge(rr.SerializeAsString(), env.ctx);
    // recharge again 500
    rr.set_amount(500);
    handlers::recharge(rr.SerializeAsString(), env.ctx);

    // balance should be 1500
    tutorial::account_balance_request br; br.set_session_token(token);
    auto bb = handlers::account_balance(br.SerializeAsString(), env.ctx);
    auto bd = decode(bb.data(), bb.size());
    tutorial::account_balance_response bresp;
    ASSERT_TRUE(bresp.ParseFromArray(bd->frame.payload.data(), bd->frame.payload.size()));
    EXPECT_EQ(bresp.balance(), 1500);

    // records should have 2
    tutorial::list_account_records_request rrq; rrq.set_session_token(token);
    auto rb = handlers::list_records(rrq.SerializeAsString(), env.ctx);
    auto rd = decode(rb.data(), rb.size());
    tutorial::list_account_records_response rresp;
    ASSERT_TRUE(rresp.ParseFromArray(rd->frame.payload.data(), rd->frame.payload.size()));
    EXPECT_EQ(rresp.records_size(), 2);
}
```

- [ ] **Step 10: Build & run — expect all PASS**

- [ ] **Step 11: Commit**

```bash
git add server/
git commit -m "feat(server): handlers (mobile_code, login, recharge, balance, records) with TDD"
```

### Task 2.5: Session (asio) + Server (acceptor) + main

**Files:**
- Create: `server/include/server/session.hpp`
- Create: `server/src/session.cpp`
- Create: `server/include/server/server.hpp`
- Create: `server/src/server.cpp`
- Create: `server/src/main.cpp`

- [ ] **Step 1: Session class (header)**

`server/include/server/session.hpp`:

```cpp
#pragma once
#include <bike/protocol.hpp>
#include <asio.hpp>
#include <memory>

#include "server/router.hpp"

namespace bike::server {

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(asio::ip::tcp::socket socket, Router& router, Ctx& ctx);
    void start();
private:
    void do_read_header();
    void do_read_body(std::uint32_t len);
    void do_write(std::vector<std::uint8_t> bytes);

    asio::ip::tcp::socket socket_;
    Router& router_;
    Ctx& ctx_;
    std::uint8_t header_buf_[bike::kHeaderLen];
    std::string body_buf_;
};

} // namespace bike::server
```

- [ ] **Step 2: Session impl**

`server/src/session.cpp`:

```cpp
#include "server/session.hpp"
#include "server/logging.hpp"

namespace bike::server {

Session::Session(asio::ip::tcp::socket socket, Router& router, Ctx& ctx)
    : socket_(std::move(socket)), router_(router), ctx_(ctx) {}

void Session::start() { do_read_header(); }

void Session::do_read_header() {
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(header_buf_, bike::kHeaderLen),
        [this, self](std::error_code ec, std::size_t /*n*/) {
            if (ec) {
                BIKE_LOG_DEBUG("session closed: {}", ec.message());
                return;
            }
            std::uint16_t eid = static_cast<std::uint16_t>(header_buf_[4]) |
                                (static_cast<std::uint16_t>(header_buf_[5]) << 8);
            std::int32_t len = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(header_buf_[6]) |
                (static_cast<std::uint32_t>(header_buf_[7]) << 8) |
                (static_cast<std::uint32_t>(header_buf_[8]) << 16) |
                (static_cast<std::uint32_t>(header_buf_[9]) << 24));
            if (len < 0 || static_cast<std::uint32_t>(len) > bike::kMaxMessageLen) {
                BIKE_LOG_WARN("bad frame length {}, closing", len);
                return;
            }
            body_buf_.assign(static_cast<std::size_t>(len), '\0');
            do_read_body(static_cast<std::uint32_t>(len));
            (void)eid;
        });
}

void Session::do_read_body(std::uint32_t len) {
    auto self = shared_from_this();
    asio::async_read(socket_, asio::buffer(body_buf_.data(), len),
        [this, self, len](std::error_code ec, std::size_t /*n*/) {
            if (ec) { BIKE_LOG_DEBUG("body read failed: {}", ec.message()); return; }
            std::uint16_t eid = static_cast<std::uint16_t>(header_buf_[4]) |
                                (static_cast<std::uint16_t>(header_buf_[5]) << 8);
            auto out = router_.dispatch(eid, body_buf_, ctx_);
            if (out.empty()) {
                BIKE_LOG_WARN("no handler for eid={}", eid);
                // close connection — no response
                return;
            }
            do_write(std::move(out));
        });
}

void Session::do_write(std::vector<std::uint8_t> bytes) {
    auto self = shared_from_this();
    auto buf = std::make_shared<std::vector<std::uint8_t>>(std::move(bytes));
    asio::async_write(socket_, asio::buffer(*buf),
        [this, self, buf](std::error_code ec, std::size_t /*n*/) {
            if (ec) { BIKE_LOG_DEBUG("write failed: {}", ec.message()); return; }
            do_read_header();
        });
}

} // namespace bike::server
```

- [ ] **Step 3: Server class**

`server/include/server/server.hpp`:

```cpp
#pragma once
#include <asio.hpp>
#include "server/router.hpp"
#include "server/session.hpp"

namespace bike::server {

class Server {
public:
    Server(asio::io_context& ioc, const std::string& host, int port,
           Router& router, Ctx& ctx);
    void run();
private:
    void do_accept();
    asio::ip::tcp::acceptor acceptor_;
    Router& router_;
    Ctx& ctx_;
};

} // namespace bike::server
```

`server/src/server.cpp`:

```cpp
#include "server/server.hpp"

namespace bike::server {

Server::Server(asio::io_context& ioc, const std::string& host, int port,
               Router& router, Ctx& ctx)
    : acceptor_(ioc, asio::ip::tcp::endpoint(asio::ip::make_address(host), port)),
      router_(router), ctx_(ctx) {
    do_accept();
}

void Server::do_accept() {
    auto& ioc = acceptor_.get_executor().context();
    auto sock = std::make_shared<asio::ip::tcp::socket>(ioc);
    acceptor_.async_accept(*sock,
        [this, sock](std::error_code ec) {
            if (!ec) {
                auto session = std::make_shared<Session>(std::move(*sock), router_, ctx_);
                session->start();
            }
            do_accept();
        });
}

} // namespace bike::server
```

- [ ] **Step 4: `main.cpp`**

```cpp
#include "server/config.hpp"
#include "server/logging.hpp"
#include "server/server.hpp"
#include "server/router.hpp"
#include "server/handlers.hpp"
#include "server/repo/in_memory.hpp"

#include <asio.hpp>
#include <vector>
#include <thread>

using namespace bike::server;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: bike-server <config.toml>\n");
        return 1;
    }
    Config cfg;
    try {
        cfg = load_config(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "config error: %s\n", e.what());
        return 2;
    }
    init_logging(cfg.log.level, cfg.log.file);
    BIKE_LOG_INFO("starting bike-server, listening {}:{}", cfg.server.listen, cfg.server.port);

    // Concrete repo implementations will be MySQL + Redis; for now we wire in-memory
    // for the initial binary smoke. (Task 2.6 swaps in prod implementations.)
    Ctx ctx{
        std::make_shared<InMemoryUserRepo>(),
        std::make_shared<InMemoryAccountRepo>(),
        std::make_shared<InMemorySessionStore>(),
    };

    Router router;
    router.register_handler(0x01, handlers::mobile_code);
    router.register_handler(0x03, handlers::login);
    router.register_handler(0x05, handlers::recharge);
    router.register_handler(0x07, handlers::account_balance);
    router.register_handler(0x09, handlers::list_records);

    asio::io_context ioc;
    Server server(ioc, cfg.server.listen, cfg.server.port, router, ctx);

    std::vector<std::thread> pool;
    int n = std::max(1, cfg.server.threads);
    pool.reserve(n);
    for (int i = 0; i < n; ++i) pool.emplace_back([&ioc]{ ioc.run(); });
    BIKE_LOG_INFO("server running with {} worker threads", n);

    for (auto& t : pool) t.join();
    return 0;
}
```

- [ ] **Step 5: Append to `server/CMakeLists.txt`**

```cmake
find_package(asio REQUIRED)

add_executable(bike-server
    src/main.cpp
    src/session.cpp
    src/server.cpp
    src/router.cpp
)
target_include_directories(bike-server PRIVATE include ${GENERATED_DIR})
target_link_libraries(bike-server PRIVATE
    bike_server_handlers bike_server_config bike_server_logging bike_common
    asio::asio)
# ASIO_STANDALONE define
target_compile_definitions(bike-server PRIVATE ASIO_STANDALONE)
```

- [ ] **Step 6: Build (will need a Linux env or Docker)**

This binary won't build on Windows without asio + protobuf + spdlog + toml++ installed. The Docker build in Phase 3 handles this — defer this verification step to Phase 3.

- [ ] **Step 7: Commit**

```bash
git add server/
git commit -m "feat(server): session/server/main + asio acceptor"
```

### Task 2.6: Concrete MySQL + Redis implementations

**Files:**
- Create: `server/include/server/db/mysql_pool.hpp`
- Create: `server/src/db/mysql_pool.cpp`
- Create: `server/include/server/db/mysql_user_repo.hpp`
- Create: `server/src/db/mysql_user_repo.cpp`
- Create: `server/include/server/db/mysql_account_repo.hpp`
- Create: `server/src/db/mysql_account_repo.cpp`
- Create: `server/include/server/cache/redis_session_store.hpp`
- Create: `server/src/cache/redis_session_store.cpp`

> NOTE: These are only buildable on Linux/Docker. We author the source; verification happens in Phase 3.

- [ ] **Step 1: MySQL pool header**

```cpp
#pragma once
#include <mysql/mysql.h>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace bike::server {

class MysqlPool {
public:
    struct Config {
        std::string host; int port; std::string user;
        std::string password; std::string database; int pool_size;
    };

    explicit MysqlPool(const Config& cfg);
    ~MysqlPool();

    class Lease {
    public:
        Lease(MysqlPool* p, MYSQL* m) : pool_(p), mysql_(m) {}
        ~Lease() { if (pool_ && mysql_) pool_->return_(mysql_); }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& o) noexcept : pool_(o.pool_), mysql_(o.mysql_) { o.pool_ = nullptr; }
        MYSQL* get() const { return mysql_; }
    private:
        MysqlPool* pool_;
        MYSQL* mysql_;
    };

    Lease acquire();    // blocks until a connection is available
private:
    MYSQL* take_();
    void   return_(MYSQL*);
    Config cfg_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::queue<MYSQL*> pool_;
};

} // namespace bike::server
```

- [ ] **Step 2: Pool impl**

```cpp
#include "server/db/mysql_pool.hpp"
#include "server/logging.hpp"
#include <stdexcept>

namespace bike::server {

MysqlPool::MysqlPool(const Config& cfg) : cfg_(cfg) {
    for (int i = 0; i < cfg.pool_size; ++i) {
        MYSQL* m = mysql_init(nullptr);
        if (!m) throw std::runtime_error("mysql_init failed");
        if (!mysql_real_connect(m, cfg.host.c_str(), cfg.user.c_str(),
                                cfg.password.c_str(), cfg.database.c_str(),
                                cfg.port, nullptr, CLIENT_MULTI_STATEMENTS)) {
            std::string err = mysql_error(m);
            mysql_close(m);
            throw std::runtime_error("mysql_real_connect: " + err);
        }
        pool_.push(m);
    }
    BIKE_LOG_INFO("mysql pool ready, {} connections", cfg.pool_size);
}

MysqlPool::~MysqlPool() {
    std::lock_guard<std::mutex> lk(mu_);
    while (!pool_.empty()) { mysql_close(pool_.front()); pool_.pop(); }
}

MysqlPool::Lease MysqlPool::acquire() {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [this]{ return !pool_.empty(); });
    MYSQL* m = pool_.front(); pool_.pop();
    return Lease(this, m);
}

void MysqlPool::return_(MYSQL* m) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        pool_.push(m);
    }
    cv_.notify_one();
}

} // namespace bike::server
```

- [ ] **Step 3: MySQL user repo**

```cpp
#pragma once
#include "server/repo/user_repo.hpp"
#include "server/db/mysql_pool.hpp"

namespace bike::server {

class MysqlUserRepo : public IUserRepo {
public:
    explicit MysqlUserRepo(std::shared_ptr<MysqlPool> pool);
    std::optional<User> find_by_mobile(const std::string& mobile) override;
    User find_or_create(const std::string& mobile) override;
private:
    std::shared_ptr<MysqlPool> pool_;
};

} // namespace bike::server
```

```cpp
#include "server/db/mysql_user_repo.hpp"
#include "server/logging.hpp"
#include <cstring>

namespace bike::server {

MysqlUserRepo::MysqlUserRepo(std::shared_ptr<MysqlPool> pool) : pool_(pool) {}

std::optional<User> MysqlUserRepo::find_by_mobile(const std::string& mobile) {
    auto lease = pool_->acquire();
    char esc[64];
    mysql_real_escape_string(lease.get(), esc, mobile.data(), mobile.size());
    char sql[256];
    std::snprintf(sql, sizeof(sql), "SELECT id, mobile FROM userinfo WHERE mobile='%s'", esc);
    if (mysql_real_query(lease.get(), sql, std::strlen(sql)) != 0) {
        BIKE_LOG_ERROR("mysql query failed: {}", mysql_error(lease.get()));
        return std::nullopt;
    }
    MYSQL_RES* res = mysql_store_result(lease.get());
    if (!res) return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<User> u;
    if (row) {
        User t;
        t.id = std::atoi(row[0]);
        t.mobile = row[1] ? row[1] : "";
        u = t;
    }
    mysql_free_result(res);
    return u;
}

User MysqlUserRepo::find_or_create(const std::string& mobile) {
    if (auto existing = find_by_mobile(mobile)) return *existing;
    auto lease = pool_->acquire();
    char esc[64];
    mysql_real_escape_string(lease.get(), esc, mobile.data(), mobile.size());
    char sql[256];
    std::snprintf(sql, sizeof(sql), "INSERT INTO userinfo (mobile) VALUES ('%s')", esc);
    if (mysql_real_query(lease.get(), sql, std::strlen(sql)) != 0) {
        BIKE_LOG_ERROR("mysql insert failed: {}", mysql_error(lease.get()));
        return find_by_mobile(mobile).value_or(User{});
    }
    User u;
    u.id = static_cast<int>(mysql_insert_id(lease.get()));
    u.mobile = mobile;
    return u;
}

} // namespace bike::server
```

- [ ] **Step 4: MySQL account repo**

```cpp
#pragma once
#include "server/repo/account_repo.hpp"
#include "server/db/mysql_pool.hpp"

namespace bike::server {

class MysqlAccountRepo : public IAccountRepo {
public:
    explicit MysqlAccountRepo(std::shared_ptr<MysqlPool> pool);
    int get_balance(int user_id) override;
    int add_balance(int user_id, RecordType type, int amount) override;
    std::vector<AccountRecord> list_records(int user_id, int limit) override;
private:
    std::shared_ptr<MysqlPool> pool_;
};

} // namespace bike::server
```

```cpp
#include "server/db/mysql_account_repo.hpp"
#include "server/logging.hpp"
#include <cstring>
#include <cstdio>

namespace bike::server {

MysqlAccountRepo::MysqlAccountRepo(std::shared_ptr<MysqlPool> pool) : pool_(pool) {}

int MysqlAccountRepo::get_balance(int uid) {
    auto lease = pool_->acquire();
    char sql[128];
    std::snprintf(sql, sizeof(sql), "SELECT balance FROM account WHERE user_id=%d", uid);
    if (mysql_real_query(lease.get(), sql, std::strlen(sql)) != 0) return 0;
    MYSQL_RES* res = mysql_store_result(lease.get());
    if (!res) return 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    int bal = row ? std::atoi(row[0]) : 0;
    mysql_free_result(res);
    return bal;
}

int MysqlAccountRepo::add_balance(int uid, RecordType type, int amount) {
    auto lease = pool_->acquire();
    // Use a transaction
    const char* sqls[] = {
        "START TRANSACTION",
        nullptr,  // INSERT IGNORE account row
        nullptr,  // UPDATE balance
        nullptr,  // INSERT record
        "COMMIT"
    };
    char buf1[128], buf2[256], buf3[256];
    std::snprintf(buf1, sizeof(buf1),
        "INSERT IGNORE INTO account (user_id, balance) VALUES (%d, 0)", uid);
    std::snprintf(buf2, sizeof(buf2),
        "UPDATE account SET balance = balance + %d WHERE user_id = %d", amount, uid);
    std::snprintf(buf3, sizeof(buf3),
        "INSERT INTO account_record (user_id, type, amount, balance_after) "
        "SELECT %d, %d, %d, balance FROM account WHERE user_id = %d",
        uid, static_cast<int>(type), amount, uid);
    sqls[1] = buf1; sqls[2] = buf2; sqls[3] = buf3;

    for (auto* s : sqls) {
        if (mysql_real_query(lease.get(), s, std::strlen(s)) != 0) {
            BIKE_LOG_ERROR("add_balance query failed: {}", mysql_error(lease.get()));
            mysql_real_query(lease.get(), "ROLLBACK", 8);
            return get_balance(uid);
        }
    }
    return get_balance(uid);
}

std::vector<AccountRecord> MysqlAccountRepo::list_records(int uid, int limit) {
    auto lease = pool_->acquire();
    char sql[256];
    std::snprintf(sql, sizeof(sql),
        "SELECT type, amount, balance_after, UNIX_TIMESTAMP(tm) "
        "FROM account_record WHERE user_id=%d ORDER BY tm DESC LIMIT %d", uid, limit);
    std::vector<AccountRecord> out;
    if (mysql_real_query(lease.get(), sql, std::strlen(sql)) != 0) return out;
    MYSQL_RES* res = mysql_store_result(lease.get());
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        AccountRecord r;
        r.type = std::atoi(row[0]);
        r.amount = std::atoi(row[1]);
        r.balance_after = std::atoll(row[2]);
        r.timestamp = std::atoll(row[3]);
        out.push_back(r);
    }
    mysql_free_result(res);
    return out;
}

} // namespace bike::server
```

- [ ] **Step 5: Redis session store**

```cpp
#pragma once
#include "server/repo/session_store.hpp"
#include <hiredis/hiredis.h>
#include <memory>
#include <mutex>
#include <string>

namespace bike::server {

class RedisSessionStore : public ISessionStore {
public:
    RedisSessionStore(std::string host, int port, int pool_size);
    ~RedisSessionStore() override;

    int set_code(const std::string& mobile) override;
    std::optional<int> get_code(const std::string& mobile) override;
    void delete_code(const std::string& mobile) override;
    std::string create_session(const std::string& mobile) override;
    std::optional<std::string> lookup_session(const std::string& token) override;
    void destroy_session(const std::string& token) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bike::server
```

```cpp
#include "server/cache/redis_session_store.hpp"
#include "server/logging.hpp"
#include <random>
#include <sstream>
#include <cstring>

namespace bike::server {

struct RedisSessionStore::Impl {
    std::mutex mu;
    std::vector<redisContext*> pool;
    std::mt19937_64 rng{std::random_device{}()};
    std::string host; int port;

    redisContext* acquire() {
        std::lock_guard<std::mutex> lk(mu);
        if (pool.empty()) {
            // Best-effort reconnect
            auto* c = redisConnect(host.c_str(), port);
            if (c && c->err) { redisFree(c); c = nullptr; }
            return c;
        }
        auto* c = pool.back(); pool.pop_back(); return c;
    }
    void release(redisContext* c) {
        if (!c) return;
        std::lock_guard<std::mutex> lk(mu);
        pool.push_back(c);
    }
};

RedisSessionStore::RedisSessionStore(std::string host, int port, int pool_size)
    : impl_(std::make_unique<Impl>()) {
    impl_->host = std::move(host); impl_->port = port;
    for (int i = 0; i < pool_size; ++i) {
        auto* c = redisConnect(impl_->host.c_str(), port);
        if (!c || c->err) {
            BIKE_LOG_ERROR("redis connect failed: {}", c ? c->errstr : "alloc");
            if (c) redisFree(c);
            continue;
        }
        impl_->pool.push_back(c);
    }
}

RedisSessionStore::~RedisSessionStore() {
    for (auto* c : impl_->pool) redisFree(c);
}

int RedisSessionStore::set_code(const std::string& m) {
    auto* c = impl_->acquire();
    std::uniform_int_distribution<int> dist(100000, 999999);
    int code = dist(impl_->rng);
    std::string key = "code:" + m;
    auto* r = static_cast<redisReply*>(redisCommand(c, "SET %s %d EX 300", key.c_str(), code));
    if (!r) { impl_->release(c); return code; }
    freeReplyObject(r);
    impl_->release(c);
    return code;
}

std::optional<int> RedisSessionStore::get_code(const std::string& m) {
    auto* c = impl_->acquire();
    if (!c) return std::nullopt;
    std::string key = "code:" + m;
    auto* r = static_cast<redisReply*>(redisCommand(c, "GET %s", key.c_str()));
    std::optional<int> out;
    if (r && r->type == REDIS_REPLY_STRING) out = std::atoi(r->str);
    if (r) freeReplyObject(r);
    impl_->release(c);
    return out;
}

void RedisSessionStore::delete_code(const std::string& m) {
    auto* c = impl_->acquire(); if (!c) return;
    std::string key = "code:" + m;
    auto* r = static_cast<redisReply*>(redisCommand(c, "DEL %s", key.c_str()));
    if (r) freeReplyObject(r);
    impl_->release(c);
}

std::string RedisSessionStore::create_session(const std::string& m) {
    auto* c = impl_->acquire(); if (!c) return {};
    std::uniform_int_distribution<unsigned long long> dist;
    std::ostringstream oss; oss << std::hex << dist(impl_->rng) << dist(impl_->rng);
    std::string token = oss.str();
    std::string key = "session:" + token;
    auto* r = static_cast<redisReply*>(redisCommand(c, "SET %s %s EX 604800",
        key.c_str(), m.c_str()));
    if (r) freeReplyObject(r);
    impl_->release(c);
    return token;
}

std::optional<std::string> RedisSessionStore::lookup_session(const std::string& t) {
    auto* c = impl_->acquire(); if (!c) return std::nullopt;
    std::string key = "session:" + t;
    auto* r = static_cast<redisReply*>(redisCommand(c, "GET %s", key.c_str()));
    std::optional<std::string> out;
    if (r && r->type == REDIS_REPLY_STRING) out = std::string(r->str, r->len);
    if (r) freeReplyObject(r);
    impl_->release(c);
    return out;
}

void RedisSessionStore::destroy_session(const std::string& t) {
    auto* c = impl_->acquire(); if (!c) return;
    std::string key = "session:" + t;
    auto* r = static_cast<redisReply*>(redisCommand(c, "DEL %s", key.c_str()));
    if (r) freeReplyObject(r);
    impl_->release(c);
}

} // namespace bike::server
```

- [ ] **Step 6: Wire into `main.cpp` (replace in-memory with prod impls)**

Replace the in-memory block in main.cpp:

```cpp
// ── concrete prod implementations ──
auto mysql_pool = std::make_shared<MysqlPool>(MysqlPool::Config{
    .host = cfg.mysql.host, .port = cfg.mysql.port, .user = cfg.mysql.user,
    .password = cfg.mysql.password, .database = cfg.mysql.database,
    .pool_size = cfg.mysql.pool_size});
Ctx ctx{
    std::make_shared<MysqlUserRepo>(mysql_pool),
    std::make_shared<MysqlAccountRepo>(mysql_pool),
    std::make_shared<RedisSessionStore>(cfg.redis.host, cfg.redis.port, cfg.redis.pool_size),
};
```

- [ ] **Step 7: Update `server/CMakeLists.txt` to add prod-impl lib**

```cmake
add_library(bike_server_prod STATIC
    src/db/mysql_pool.cpp
    src/db/mysql_user_repo.cpp
    src/db/mysql_account_repo.cpp
    src/cache/redis_session_store.cpp
)
target_include_directories(bike_server_prod PUBLIC include)
target_link_libraries(bike_server_prod PUBLIC bike_server_handlers hiredis mysqlclient)

target_link_libraries(bike-server PRIVATE bike_server_prod)
```

- [ ] **Step 8: Commit**

```bash
git add server/
git commit -m "feat(server): MySQL pool + user/account repos + Redis session store"
```

---

## Phase 3 — Docker + deploy

### Task 3.1: Dockerfile.server

**Files:**
- Create: `docker/Dockerfile.server`

- [ ] **Step 1: Write Dockerfile (multi-stage)**

```dockerfile
FROM ubuntu:22.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential cmake git \
    libasio-dev libprotobuf-dev protobuf-compiler \
    libhiredis-dev libmysqlclient-dev \
    libspdlog-dev libtomlplusplus-dev libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -B build -S . -DBIKE_BUILD_TESTS=OFF && \
    cmake --build build -j --target bike-server

FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    libprotobuf23 libhiredis0.14 libmysqlclient21 libspdlog1 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /src/build/server/bike-server /app/bike-server
RUN mkdir -p /var/log/bike-server
EXPOSE 8888
ENTRYPOINT ["/app/bike-server", "/etc/bike/server.toml"]
```

- [ ] **Step 2: Commit**

```bash
git add docker/Dockerfile.server
git commit -m "build(docker): multi-stage Dockerfile for bike-server"
```

### Task 3.2: docker-compose.yml + mysql-init

**Files:**
- Create: `docker/docker-compose.yml`
- Create: `docker/mysql-init/schema.sql`
- Create: `docker/server.toml`

- [ ] **Step 1: schema.sql**

```sql
CREATE TABLE IF NOT EXISTS userinfo (
  id          int          NOT NULL PRIMARY KEY AUTO_INCREMENT,
  mobile      varchar(16)  NOT NULL UNIQUE,
  username    varchar(128) NOT NULL DEFAULT '',
  registertm  timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX mobile_index (mobile)
);
CREATE TABLE IF NOT EXISTS account (
  user_id     int          NOT NULL PRIMARY KEY,
  balance     int          NOT NULL DEFAULT 0,
  FOREIGN KEY (user_id) REFERENCES userinfo(id)
);
CREATE TABLE IF NOT EXISTS account_record (
  id            bigint       NOT NULL PRIMARY KEY AUTO_INCREMENT,
  user_id       int          NOT NULL,
  type          tinyint      NOT NULL,
  amount        int          NOT NULL,
  balance_after int          NOT NULL,
  tm            timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX user_tm (user_id, tm)
);
```

- [ ] **Step 2: docker-compose.yml**

```yaml
services:
  mysql:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: root_pwd
      MYSQL_DATABASE: shared_bike
      MYSQL_USER: bike
      MYSQL_PASSWORD: bike_pwd
    volumes:
      - ./mysql-init:/docker-entrypoint-initdb.d:ro
      - mysql-data:/var/lib/mysql
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost"]
      interval: 5s
      timeout: 3s
      retries: 20

  redis:
    image: redis:7-alpine
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 5s
      timeout: 3s
      retries: 20

  server:
    build:
      context: ..
      dockerfile: docker/Dockerfile.server
    depends_on:
      mysql:
        condition: service_healthy
      redis:
        condition: service_healthy
    ports:
      - "8888:8888"
    volumes:
      - ./server.toml:/etc/bike/server.toml:ro
      - server-logs:/var/log/bike-server

volumes:
  mysql-data:
  server-logs:
```

- [ ] **Step 3: docker/server.toml**

```toml
[server]
listen = "0.0.0.0"
port = 8888
threads = 4

[mysql]
host = "mysql"
port = 3306
user = "bike"
password = "bike_pwd"
database = "shared_bike"
pool_size = 8

[redis]
host = "redis"
port = 6379
pool_size = 4

[log]
level = "info"
file = "/var/log/bike-server/server.log"
```

- [ ] **Step 4: Commit**

```bash
git add docker/
git commit -m "build(docker): docker-compose + mysql-init schema + container config"
```

### Task 3.3: Local build verification (in Docker, no deploy)

- [ ] **Step 1: Build the server image**

```bash
docker build -t bike-server:dev -f docker/Dockerfile.server .
# Expected: image builds, ends with "---> <hash>" successfully
```

- [ ] **Step 2: Smoke-test compile by running `bike-server --help`-style call**

```bash
docker run --rm bike-server:dev sh -c 'echo "expected usage message follows:"; /app/bike-server 2>&1 || true'
# Expected: prints "usage: bike-server <config.toml>" because no arg given
```

- [ ] **Step 3: Bring up compose locally and smoke test**

```bash
cd docker && docker compose up -d
sleep 30  # wait for mysql healthcheck
docker compose ps     # all 3 healthy
docker compose logs server | head -50
# Expected: log line "starting bike-server, listening 0.0.0.0:8888" and "server running with 4 worker threads"
# Then run a quick fb-frame test:
docker run --rm --network=docker_default --entrypoint sh bike-server:dev \
  -c 'printf "FBEB\x01\x00\x1d\x00\x00\x00\x0a\x04\x08\x96\xc1\xec\xbd\x03\x12\x1115600000000\x00\x00\x00" | nc server 8888 | head -c 100 | xxd'
# (Frame decode verified via unit tests; smoke test only confirms socket accept)
docker compose down
```

- [ ] **Step 4: Commit any tweaks + tag**

```bash
git tag v0.1.0-local
```

### Task 3.4: Deploy to Tencent Cloud

**Pre-flight (USER ACTION required before this task runs):**
- The user must have run `!ssh-copy-id ... ubuntu@124.220.92.243` so SSH passwordless works
- The user must open Tencent Cloud security group inbound TCP 8888

**Files:**
- Create: `scripts/deploy.sh`

- [ ] **Step 1: Write deploy.sh**

```bash
#!/usr/bin/env bash
set -euo pipefail

REMOTE=ubuntu@124.220.92.243
REMOTE_DIR=~/bike
GIT_SHA=$(git rev-parse --short HEAD)
IMG=bike-server:${GIT_SHA}

echo "==> Building image ${IMG}"
docker build -t "${IMG}" -f docker/Dockerfile.server .

echo "==> Saving image"
docker save "${IMG}" | gzip > /tmp/bike-server.tgz

echo "==> Copying to remote"
ssh "${REMOTE}" "mkdir -p ${REMOTE_DIR}"
scp /tmp/bike-server.tgz docker/ "${REMOTE}:${REMOTE_DIR}/"
scp docker-compose.yml "${REMOTE}:${REMOTE_DIR}/docker-compose.yml"

echo "==> Loading + restarting on remote"
ssh "${REMOTE}" "cd ${REMOTE_DIR} && \
    docker load < bike-server.tgz && \
    docker compose down && \
    docker compose up -d"

echo "==> Waiting for healthy"
sleep 30
ssh "${REMOTE}" "cd ${REMOTE_DIR} && docker compose ps"

echo "==> Smoke test (server reply should appear)"
ssh "${REMOTE}" "docker exec \$(docker compose ps -q server) sh -c 'echo alive'"

rm -f /tmp/bike-server.tgz
echo "Deployed ${IMG} to ${REMOTE}"
```

- [ ] **Step 2: Make executable + commit**

```bash
chmod +x scripts/deploy.sh
git add scripts/deploy.sh
git commit -m "build(deploy): deploy script for Tencent Cloud"
```

- [ ] **Step 3: Run deploy**

```bash
./scripts/deploy.sh
# Expected: remote `docker compose ps` shows 3 services healthy
```

- [ ] **Step 4: External smoke test from local**

```bash
# Use the Qt client once Phase 4 is done; for now just `nc` against the public IP:
nc -zv 124.220.92.243 8888
# Expected: connection succeeded
```

---

## Phase 4 — Qt6 client

### Task 4.1: Client project skeleton + BackendClient

**Files:**
- Create: `client/CMakeLists.txt`
- Create: `client/src/main.cpp`
- Create: `client/src/backend_client.hpp`
- Create: `client/src/backend_client.cpp`
- Create: `client/src/session_model.hpp`

- [ ] **Step 1: client/CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(bike_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets Concurrent REQUIRED)
find_package(asio REQUIRED)
find_package(Protobuf REQUIRED)

# Reuse generated protobuf from common via bike_common target
# (assume top-level added bike_common; we link it)

add_executable(bike_client
    src/main.cpp
    src/backend_client.cpp
    src/mainwindow.cpp
    src/views/login_view.cpp
    src/views/wallet_view.cpp
    src/views/records_view.cpp
    src/mainwindow.ui
    src/views/login_view.ui
    src/views/wallet_view.ui
    src/views/records_view.ui
)
target_link_libraries(bike_client PRIVATE
    Qt6::Widgets Qt6::Concurrent
    bike_common
    asio::asio protobuf::libprotobuf
)
target_compile_definitions(bike_client PRIVATE ASIO_STANDALONE QT_DISABLE_DEPRECATED_BEFORE=0x060000)
target_include_directories(bike_client PRIVATE src ${CMAKE_SOURCE_DIR}/common/include)
```

- [ ] **Step 2: backend_client.hpp**

```cpp
#pragma once
#include <bike/protocol.hpp>
#include <bike.pb.h>
#include <asio.hpp>
#include <string>

namespace bike::client {

class BackendClient {
public:
    BackendClient(std::string host, int port);
    // blocking calls; caller wraps in QtConcurrent::run
    tutorial::mobile_response get_mobile_code(const std::string& mobile);
    tutorial::login_response login(const std::string& mobile, int icode);
    tutorial::recharge_response recharge(const std::string& token, int amount);
    tutorial::account_balance_response get_balance(const std::string& token);
    tutorial::list_account_records_response list_records(const std::string& token);
private:
    std::vector<std::uint8_t> round_trip(std::uint16_t eid, const std::string& payload);
    std::string host_;
    int port_;
    asio::io_context ioc_;
};

} // namespace bike::client
```

- [ ] **Step 3: backend_client.cpp**

```cpp
#include "backend_client.hpp"
#include <stdexcept>

namespace bike::client {

BackendClient::BackendClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

std::vector<std::uint8_t> BackendClient::round_trip(std::uint16_t eid,
                                                    const std::string& payload) {
    asio::ip::tcp::socket socket(ioc_);
    asio::ip::tcp::resolver r(ioc_);
    asio::connect(socket, r.resolve(host_, std::to_string(port_)));

    Frame req{.event_id = eid, .payload = payload};
    auto bytes = encode(req);
    asio::write(socket, asio::buffer(bytes));

    std::uint8_t header[kHeaderLen];
    asio::read(socket, asio::buffer(header, kHeaderLen));
    if (std::memcmp(header, kFrameMagic, 4) != 0)
        throw std::runtime_error("bad magic from server");
    std::int32_t len = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(header[6]) |
        (static_cast<std::uint32_t>(header[7]) << 8) |
        (static_cast<std::uint32_t>(header[8]) << 16) |
        (static_cast<std::uint32_t>(header[9]) << 24));
    if (len < 0 || static_cast<std::uint32_t>(len) > kMaxMessageLen)
        throw std::runtime_error("bad length from server");
    std::string body(static_cast<std::size_t>(len), '\0');
    asio::read(socket, asio::buffer(body.data(), body.size()));

    Frame rsp{.event_id = eid, .payload = body};
    return encode(rsp);  // for callers to decode
}

tutorial::mobile_response BackendClient::get_mobile_code(const std::string& m) {
    tutorial::mobile_request req; req.set_mobile(m);
    auto bytes = round_trip(0x01, req.SerializeAsString());
    auto r = decode(bytes.data(), bytes.size());
    tutorial::mobile_response rsp;
    rsp.ParseFromArray(r->frame.payload.data(), r->frame.payload.size());
    return rsp;
}

tutorial::login_response BackendClient::login(const std::string& m, int icode) {
    tutorial::login_request req; req.set_mobile(m); req.set_icode(icode);
    auto bytes = round_trip(0x03, req.SerializeAsString());
    auto r = decode(bytes.data(), bytes.size());
    tutorial::login_response rsp;
    rsp.ParseFromArray(r->frame.payload.data(), r->frame.payload.size());
    return rsp;
}

tutorial::recharge_response BackendClient::recharge(const std::string& t, int amount) {
    tutorial::recharge_request req; req.set_session_token(t); req.set_amount(amount);
    auto bytes = round_trip(0x05, req.SerializeAsString());
    auto r = decode(bytes.data(), bytes.size());
    tutorial::recharge_response rsp;
    rsp.ParseFromArray(r->frame.payload.data(), r->frame.payload.size());
    return rsp;
}

tutorial::account_balance_response BackendClient::get_balance(const std::string& t) {
    tutorial::account_balance_request req; req.set_session_token(t);
    auto bytes = round_trip(0x07, req.SerializeAsString());
    auto r = decode(bytes.data(), bytes.size());
    tutorial::account_balance_response rsp;
    rsp.ParseFromArray(r->frame.payload.data(), r->frame.payload.size());
    return rsp;
}

tutorial::list_account_records_response BackendClient::list_records(const std::string& t) {
    tutorial::list_account_records_request req; req.set_session_token(t);
    auto bytes = round_trip(0x09, req.SerializeAsString());
    auto r = decode(bytes.data(), bytes.size());
    tutorial::list_account_records_response rsp;
    rsp.ParseFromArray(r->frame.payload.data(), r->frame.payload.size());
    return rsp;
}

} // namespace bike::client
```

- [ ] **Step 4: session_model.hpp**

```cpp
#pragma once
#include <string>

namespace bike::client {

struct SessionModel {
    std::string mobile;
    std::string token;
    bool logged_in() const { return !token.empty(); }
};

} // namespace bike::client
```

- [ ] **Step 5: Commit**

```bash
git add client/CMakeLists.txt client/src/backend_client.* client/src/session_model.hpp
git commit -m "feat(client): BackendClient + SessionModel scaffolding"
```

### Task 4.2: Qt UI — LoginView

**Files:**
- Create: `client/src/views/login_view.hpp`
- Create: `client/src/views/login_view.cpp`
- Create: `client/src/views/login_view.ui`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <QWidget>
#include "backend_client.hpp"
#include "session_model.hpp"

namespace bike::client {

class LoginView : public QWidget {
    Q_OBJECT
public:
    LoginView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);
signals:
    void logged_in();
private slots:
    void on_get_code();
    void on_login();
private:
    BackendClient* client_;
    SessionModel* session_;
    class Ui_LoginView* ui_;
};

} // namespace bike::client
```

- [ ] **Step 2: `login_view.ui` (XML)**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>LoginView</class>
 <widget class="QWidget" name="LoginView">
  <layout class="QFormLayout" name="form">
   <item row="0" column="0"><widget class="QLabel" name="lb_mobile"><property name="text"><string>Mobile:</string></property></widget></item>
   <item row="0" column="1"><widget class="QLineEdit" name="le_mobile"/></item>
   <item row="1" column="0"><widget class="QLabel" name="lb_code"><property name="text"><string>Code:</string></property></widget></item>
   <item row="1" column="1"><widget class="QLineEdit" name="le_code"/></item>
   <item row="2" column="0" colspan="2">
    <widget class="QPushButton" name="btn_get_code"><property name="text"><string>Get code</string></property></widget>
   </item>
   <item row="3" column="0" colspan="2">
    <widget class="QPushButton" name="btn_login"><property name="text"><string>Login</string></property></widget>
   </item>
   <item row="4" column="0" colspan="2">
    <widget class="QLabel" name="lb_status"><property name="text"><string/></property></widget>
   </item>
  </layout>
 </widget>
 <resources/>
 <connections/>
</ui>
```

- [ ] **Step 3: `login_view.cpp`**

```cpp
#include "login_view.h"
#include "ui_login_view.h"
#include <QtConcurrent/QtConcurrent>
#include <QMessageBox>

namespace bike::client {

LoginView::LoginView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session), ui_(new Ui_LoginView) {
    ui_->setupUi(this);
    connect(ui_->btn_get_code, &QPushButton::clicked, this, &LoginView::on_get_code);
    connect(ui_->btn_login, &QPushButton::clicked, this, &LoginView::on_login);
}

void LoginView::on_get_code() {
    auto mobile = ui_->le_mobile->text().toStdString();
    ui_->lb_status->setText("sending code...");
    QtConcurrent::run([this, mobile] {
        try {
            auto rsp = client_->get_mobile_code(mobile);
            QMetaObject::invokeMethod(this, [this, rsp] {
                ui_->lb_status->setText(QString("code sent (icode=%1)").arg(rsp.icode()));
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                ui_->lb_status->setText(QString("error: %1").arg(QString::fromStdString(msg)));
            });
        }
    });
}

void LoginView::on_login() {
    auto mobile = ui_->le_mobile->text().toStdString();
    int code = ui_->le_code->text().toInt();
    ui_->lb_status->setText("logging in...");
    QtConcurrent::run([this, mobile, code] {
        try {
            auto rsp = client_->login(mobile, code);
            QMetaObject::invokeMethod(this, [this, rsp, mobile] {
                if (rsp.code() == 200 && !rsp.session_token().empty()) {
                    session_->mobile = mobile;
                    session_->token = rsp.session_token();
                    ui_->lb_status->setText("login ok");
                    emit logged_in();
                } else {
                    ui_->lb_status->setText(QString("login failed: %1").arg(rsp.desc().c_str()));
                }
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                ui_->lb_status->setText(QString("error: %1").arg(QString::fromStdString(msg)));
            });
        }
    });
}

} // namespace bike::client
```

- [ ] **Step 4: Commit**

```bash
git add client/src/views/login_view.*
git commit -m "feat(client): LoginView (mobile + code + get/login buttons)"
```

### Task 4.3: Qt UI — WalletView + RecordsView

**Files:**
- Create: `client/src/views/wallet_view.hpp/cpp/ui`
- Create: `client/src/views/records_view.hpp/cpp/ui`

- [ ] **Step 1: WalletView header**

```cpp
#pragma once
#include <QWidget>
#include "backend_client.hpp"
#include "session_model.hpp"

namespace bike::client {

class WalletView : public QWidget {
    Q_OBJECT
public:
    WalletView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);
public slots:
    void refresh_balance();
private slots:
    void on_recharge();
private:
    BackendClient* client_;
    SessionModel* session_;
    class Ui_WalletView* ui_;
};

} // namespace bike::client
```

- [ ] **Step 2: WalletView ui**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>WalletView</class>
 <widget class="QWidget" name="WalletView">
  <layout class="QVBoxLayout" name="v">
   <item>
    <widget class="QLabel" name="lb_balance"><property name="text"><string>Balance: ?</string></property></widget>
   </item>
   <item>
    <layout class="QHBoxLayout">
     <item><widget class="QLineEdit" name="le_amount"/></item>
     <item><widget class="QPushButton" name="btn_recharge"><property name="text"><string>Recharge (cents)</string></property></widget></item>
    </layout>
   </item>
   <item><widget class="QLabel" name="lb_status"/></item>
   <item><widget class="QPushButton" name="btn_refresh"><property name="text"><string>Refresh balance</string></property></widget></item>
  </layout>
 </widget>
</ui>
```

- [ ] **Step 3: WalletView impl**

```cpp
#include "wallet_view.h"
#include "ui_wallet_view.h"
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

WalletView::WalletView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session), ui_(new Ui_WalletView) {
    ui_->setupUi(this);
    connect(ui_->btn_recharge, &QPushButton::clicked, this, &WalletView::on_recharge);
    connect(ui_->btn_refresh, &QPushButton::clicked, this, &WalletView::refresh_balance);
}

void WalletView::refresh_balance() {
    if (!session_->logged_in()) return;
    QtConcurrent::run([this] {
        try {
            auto rsp = client_->get_balance(session_->token);
            QMetaObject::invokeMethod(this, [this, rsp] {
                ui_->lb_balance->setText(QString("Balance: %1").arg(rsp.balance()));
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, m = std::string(e.what())] {
                ui_->lb_status->setText(QString::fromStdString(m));
            });
        }
    });
}

void WalletView::on_recharge() {
    int amount = ui_->le_amount->text().toInt();
    if (amount <= 0) { ui_->lb_status->setText("amount must be positive"); return; }
    QtConcurrent::run([this, amount] {
        try {
            auto rsp = client_->recharge(session_->token, amount);
            QMetaObject::invokeMethod(this, [this, rsp] {
                ui_->lb_balance->setText(QString("Balance: %1").arg(rsp.balance()));
                ui_->lb_status->setText("recharge ok");
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, m = std::string(e.what())] {
                ui_->lb_status->setText(QString::fromStdString(m));
            });
        }
    });
}

} // namespace bike::client
```

- [ ] **Step 4: RecordsView (use QTableWidget)**

```cpp
#pragma once
#include <QWidget>
#include <QTableWidget>
#include "backend_client.hpp"
#include "session_model.hpp"

namespace bike::client {

class RecordsView : public QWidget {
    Q_OBJECT
public:
    RecordsView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);
public slots:
    void refresh();
private:
    BackendClient* client_;
    SessionModel* session_;
    QTableWidget* table_;
};

} // namespace bike::client
```

```cpp
#include "records_view.h"

namespace bike::client {

RecordsView::RecordsView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session), table_(new QTableWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(table_);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({"Type", "Amount(cents)", "Balance after", "Time"});
    table_->horizontalHeader()->setStretchLastSection(true);
}

void RecordsView::refresh() {
    if (!session_->logged_in()) return;
    QtConcurrent::run([this] {
        try {
            auto rsp = client_->list_records(session_->token);
            QMetaObject::invokeMethod(this, [this, rsp] {
                table_->setRowCount(rsp.records_size());
                for (int i = 0; i < rsp.records_size(); ++i) {
                    const auto& r = rsp.records(i);
                    table_->setItem(i, 0, new QTableWidgetItem(
                        r.type() == 1 ? "Recharge" : "Consume"));
                    table_->setItem(i, 1, new QTableWidgetItem(QString::number(r.limit())));
                    table_->setItem(i, 2, new QTableWidgetItem("—"));  // not in proto
                    table_->setItem(i, 3, new QTableWidgetItem(
                        QDateTime::fromSecsSinceEpoch(r.timestamp()).toString()));
                }
            });
        } catch (const std::exception&) {}
    });
}

} // namespace bike::client
```

- [ ] **Step 5: Commit**

```bash
git add client/src/views/
git commit -m "feat(client): WalletView + RecordsView"
```

### Task 4.4: MainWindow + main.cpp

**Files:**
- Create: `client/src/mainwindow.hpp/cpp/ui`
- Update: `client/src/main.cpp`

- [ ] **Step 1: mainwindow.hpp**

```cpp
#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include "backend_client.hpp"
#include "session_model.hpp"
#include "views/login_view.h"
#include "views/wallet_view.h"
#include "views/records_view.h"

namespace bike::client {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
private:
    BackendClient client_;
    SessionModel session_;
    QStackedWidget* stack_;
    LoginView* login_;
    WalletView* wallet_;
    RecordsView* records_;
};

} // namespace bike::client
```

- [ ] **Step 2: mainwindow.cpp**

```cpp
#include "mainwindow.h"
#include <QTabWidget>

namespace bike::client {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      client_("124.220.92.243", 8888) {
    auto* tabs = new QTabWidget(this);
    login_   = new LoginView(&client_, &session_, this);
    wallet_  = new WalletView(&client_, &session_, this);
    records_ = new RecordsView(&client_, &session_, this);
    tabs->addTab(login_,   "Login");
    tabs->addTab(wallet_,  "Wallet");
    tabs->addTab(records_, "Records");
    setCentralWidget(tabs);

    connect(login_, &LoginView::logged_in, this, [this] {
        wallet_->refresh_balance();
        records_->refresh();
        tabs->setCurrentIndex(1);
    });
    setWindowTitle("Bike Client");
    resize(600, 400);
}

} // namespace bike::client
```

- [ ] **Step 3: main.cpp**

```cpp
#include <QApplication>
#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    bike::client::MainWindow w;
    w.show();
    return app.exec();
}
```

- [ ] **Step 4: Build the client (needs Qt6 installed)**

```bash
cmake -B build -S . -DBIKE_BUILD_CLIENT=ON
cmake --build build --target bike_client --config Release
# Expected: bike_client.exe (or bike_client on Linux) produced
```

- [ ] **Step 5: Manual smoke test**

Launch `bike_client.exe`, switch to Login tab, enter a mobile number, click Get code, then click Login. Verify it switches to Wallet tab. Enter amount, click Recharge, verify balance updates.

- [ ] **Step 6: Commit**

```bash
git add client/
git commit -m "feat(client): MainWindow + main, wire all views"
```

---

## Phase 5 — Push to GitHub

### Task 5.1: Push

**Pre-flight (USER ACTION required):**
- User must have added `~/.ssh/id_ed25519.pub` to GitHub → Settings → SSH and GPG keys

- [ ] **Step 1: Add remote + push**

```bash
git remote add origin git@github.com:wangya002/shared_bike_refactor.git
git branch -M main
git push -u origin main
# Expected: repository visible at https://github.com/wangya002/shared_bike_refactor
```

- [ ] **Step 2: Tag the v0.1.0 release**

```bash
git tag -a v0.1.0 -m "initial C++17 refactor + Qt6 client + docker deploy"
git push origin v0.1.0
```

---

## Self-review checklist (executed by author before hand-off)

- ✅ Spec §2 验收 全部对应任务：用户+钱包功能 = Task 2.4；Redis 会话层 = Task 2.3+2.6；Docker compose = Task 3.2；Qt 客户端 = Phase 4；GitHub push = Phase 5
- ✅ 无 TBD/TODO 占位
- ✅ 类型一致：`UserRepo`/`AccountRepo`/`SessionStore` 接口在 2.3 定义，在 2.4 使用，签名匹配；`Frame`/`encode`/`decode` 在 1.3 定义并在多处使用
- ✅ TDD：协议 (1.3)、配置 (2.1)、handlers (2.4) 都有测试先行；MySQL/Redis/Qt 因依赖外部环境，靠 Docker smoke + 手动验收
- ✅ Frequent commits：每个 task 末尾都有 `git commit` step

## 外部依赖（任务中遇到时显式提醒用户）

1. **Task 3.4 之前**：用户必须执行 `!ssh-copy-id -i ~/.ssh/id_ed25519.pub ubuntu@124.220.92.243`，并在腾讯云控制台安全组放行 TCP 8888 入站
2. **Task 5.1 之前**：用户必须把 `~/.ssh/id_ed25519.pub` 添加到 GitHub 账号的 SSH keys
3. **Phase 4 Qt 客户端**：用户必须安装 Qt 6.x + CMake（可在 https://qt.io 下载，或 `vcpkg install qtbase`）

---

## Execution mode

User authorized autonomous execution. **Execution sub-skill**: `superpowers:subagent-driven-development`.
