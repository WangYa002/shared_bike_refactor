# 共享单车客户端扩展 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 Qt 客户端(登录 / 钱包 / 账单)基础上,扩展地图、骑行、历史 5-tab UI,与服务端新事件对接,完成"扫码 → 骑行 → 扣费 → 回放"闭环。

**Architecture:** 客户端 Qt6 Widgets + QWebEngineView(地图)+ QtConcurrent(异步 IO)。`TrajectorySim` 本地生成模拟轨迹,`BackendClient` 扩展 7 个方法 + 独立 socket 上报位置,`QWebChannel` 桥接 C++ 与 Leaflet JS。主窗口改为 `QStackedWidget`(登录 ↔ 5-tab 容器)。

**Tech Stack:** Qt6 Widgets / WebEngineWidgets / WebChannel / Concurrent / Positioning,C++17,asio,protobuf,`std::mt19937`(轨迹模拟),GTest

**Spec reference:** `docs/superpowers/specs/2026-07-19-shared-bike-expansion-design.md` 第 4 节(客户端 UX)、6.6(BackendClient)、6.7(TrajectorySim)、7.6(崩溃恢复)、8.7-8.8(不测 Qt UI,靠人工 smoke)

**Depends on:** Server-side plan(`docs/superpowers/plans/2026-07-19-shared-bike-server-expansion.md`)必须先跑通,否则没有事件可对接。

---

## File Structure

### 新增文件
```
client/
  src/
    trajectory_sim.hpp / .cpp       本地轨迹生成器(可单测,deterministic)
    location_provider.hpp / .cpp    Qt Positioning 抽象 + 默认坐标 fallback
    map_bridge.hpp / .cpp           C++ ↔ JS 桥(QObject,slots 给 JS 调)
    views/
      map_view.hpp / .cpp           地图 tab
      ride_view.hpp / .cpp          骑行中 tab
      ride_history_view.hpp / .cpp  历史 tab
      ride_detail_dialog.hpp / .cpp 轨迹回放对话框
  resources/
    map.qrc                         打包 map.html / map.js / map.css
    map.html
    map.js
    map.css
  tests/
    test_trajectory_sim.cpp         GTest 单元
docs/
  smoke_checklist.md                手动 smoke 清单
```

### 修改文件
- `client/CMakeLists.txt` — 加 Qt WebEngine/WebChannel/Positioning/Test,新增源文件、`bike_client_tests` 目标
- `client/src/backend_client.hpp` — 新增 7 个方法声明 + `pos_socket_` 字段
- `client/src/backend_client.cpp` — 实现 7 个方法
- `client/src/mainwindow.hpp` — 改成 `QStackedWidget`,新增 5 个 view 字段、`active_ride` 字段
- `client/src/mainwindow.cpp` — 重构主窗口、tab 启用/禁用、`QSettings` 恢复

---

### Task 1: CMake + Qt 模块扩展

**Files:**
- Modify: `client/CMakeLists.txt`

- [ ] **Step 1: 编辑 CMakeLists.txt,加 Qt 模块**

完整替换为:

```cmake
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

# ---- 单元测试 ----
find_package(Qt6 COMPONENTS Test QUIET)
if(Qt6Test_FOUND)
    enable_testing()
    add_executable(bike_client_tests
        tests/test_trajectory_sim.cpp
        src/trajectory_sim.cpp
    )
    target_link_libraries(bike_client_tests PRIVATE
        Qt6::Test
        Qt6::Core
    )
    target_include_directories(bike_client_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME bike_client_tests COMMAND bike_client_tests)
endif()
```

- [ ] **Step 2: 运行配置阶段验证 CMake 解析通过**

Run: `cmake -B build -S .`
Expected: 无报错(可以 warning Qt6::Test 未找到,但不应 fail)。如果 Qt6 没装 WebEngine,会报 "Could not find a package configuration file provided by Qt6WebEngineWidgets",需要先 `vcpkg install qtwebengine` 或 Qt Maintenance Tool 装 WebEngine。

- [ ] **Step 3: 临时把 backend_client.cpp 留作桩,确认骨架可编**

为了让 Task 1 单独编过(此时新文件还不存在),先暂时把 CMakeLists 里的新源文件注释掉,只保留 Qt 模块开关。Step 3 仅作为"配置 + Qt 链接是否成功"的烟雾测试。

- [ ] **Step 4: 恢复完整源文件列表(取消注释)**

确保后续 Task 创建对应文件时,CMake 能直接编译。

- [ ] **Step 5: Commit**

```bash
git add client/CMakeLists.txt
git commit -m "build(client): enable Qt WebEngine/WebChannel/Positioning + test target"
```

---

### Task 2: BackendClient 头文件 — 7 个新方法声明

**Files:**
- Modify: `client/src/backend_client.hpp`

- [ ] **Step 1: 修改 backend_client.hpp,新增 7 个方法 + 独立 socket 字段**

完整新版本:

```cpp
#pragma once

#include <bike/protocol.hpp>
#include <bike.pb.h>

#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace bike::client {

class BackendError : public std::runtime_error {
public:
    BackendError(const std::string& msg) : std::runtime_error(msg) {}
};

class BackendClient {
public:
    BackendClient(std::string host, int port);

    // 现有方法
    tutorial::mobile_response              get_mobile_code(const std::string& mobile);
    tutorial::login_response               login(const std::string& mobile, int icode);
    tutorial::recharge_response            recharge(const std::string& token, int amount);
    tutorial::account_balance_response     get_balance(const std::string& token);
    tutorial::list_account_records_response list_records(const std::string& token);

    // 新增 7 个方法
    tutorial::list_nearby_bikes_response list_nearby_bikes(
        const std::string& token, double lat, double lng, double radius_m);
    tutorial::scan_unlock_response scan_unlock(
        const std::string& token, const std::string& bike_no,
        double lat, double lng);
    void report_position(
        const std::string& ride_no, int seq,
        double lat, double lng, int elapsed_sec);  // 单向,fire-and-forget
    tutorial::end_ride_response end_ride(
        const std::string& token, const std::string& ride_no,
        double lat, double lng);
    tutorial::report_damage_response report_damage(
        const std::string& token, const std::string& bike_no,
        const std::string& note);
    tutorial::get_ride_detail_response get_ride_detail(
        const std::string& token, const std::string& ride_no);
    tutorial::list_rides_response list_rides(
        const std::string& token, int limit);

private:
    std::vector<std::uint8_t> round_trip(std::uint16_t eid, const std::string& payload);

    std::string host_;
    int port_;
    asio::io_context ioc_;
    std::atomic<bool> pos_sending_{false};  // 上报位置时的 backpressure flag
};

} // namespace bike::client
```

- [ ] **Step 2: 暂不实现,只验证头可解析**

不需要单独 build,会随 Task 3 一起编。

- [ ] **Step 3: Commit**

```bash
git add client/src/backend_client.hpp
git commit -m "feat(client): declare 7 new ride-related BackendClient methods"
```

---

### Task 3: BackendClient 实现 — 6 个 round-trip 方法

**Files:**
- Modify: `client/src/backend_client.cpp`

- [ ] **Step 1: 在 backend_client.cpp 末尾(`} // namespace`) 之前追加 6 个方法实现**

```cpp
tutorial::list_nearby_bikes_response BackendClient::list_nearby_bikes(
    const std::string& token, double lat, double lng, double radius_m) {
    tutorial::list_nearby_bikes_request req;
    req.set_session_token(token);
    req.set_lat(lat);
    req.set_lng(lng);
    req.set_radius_m(radius_m);
    auto bytes = round_trip(0x11, req.SerializeAsString());
    return parse<tutorial::list_nearby_bikes_response>(bytes);
}

tutorial::scan_unlock_response BackendClient::scan_unlock(
    const std::string& token, const std::string& bike_no,
    double lat, double lng) {
    tutorial::scan_unlock_request req;
    req.set_session_token(token);
    req.set_bike_no(bike_no);
    req.set_lat(lat);
    req.set_lng(lng);
    auto bytes = round_trip(0x13, req.SerializeAsString());
    return parse<tutorial::scan_unlock_response>(bytes);
}

tutorial::end_ride_response BackendClient::end_ride(
    const std::string& token, const std::string& ride_no,
    double lat, double lng) {
    tutorial::end_ride_request req;
    req.set_session_token(token);
    req.set_ride_no(ride_no);
    req.set_end_lat(lat);
    req.set_end_lng(lng);
    auto bytes = round_trip(0x17, req.SerializeAsString());
    return parse<tutorial::end_ride_response>(bytes);
}

tutorial::report_damage_response BackendClient::report_damage(
    const std::string& token, const std::string& bike_no,
    const std::string& note) {
    tutorial::report_damage_request req;
    req.set_session_token(token);
    req.set_bike_no(bike_no);
    req.set_note(note);
    auto bytes = round_trip(0x19, req.SerializeAsString());
    return parse<tutorial::report_damage_response>(bytes);
}

tutorial::get_ride_detail_response BackendClient::get_ride_detail(
    const std::string& token, const std::string& ride_no) {
    tutorial::get_ride_detail_request req;
    req.set_session_token(token);
    req.set_ride_no(ride_no);
    auto bytes = round_trip(0x1B, req.SerializeAsString());
    return parse<tutorial::get_ride_detail_response>(bytes);
}

tutorial::list_rides_response BackendClient::list_rides(
    const std::string& token, int limit) {
    tutorial::list_rides_request req;
    req.set_session_token(token);
    req.set_limit(limit);
    auto bytes = round_trip(0x1D, req.SerializeAsString());
    return parse<tutorial::list_rides_response>(bytes);
}
```

- [ ] **Step 2: 编译验证(此时 report_position 还没实现,会报 undefined reference)**

预期:链接错误,提示 `BackendClient::report_position` 未定义 —— 这是 Task 4 要补齐的。这一步只是确认 6 个新方法编译通过。

- [ ] **Step 3: Commit(与 Task 4 合并 commit,避免破窗构建)**

暂不 commit。完成 Task 4 后一起 commit。

---

### Task 4: BackendClient 实现 — 位置上报(独立 socket + backpressure)

**Files:**
- Modify: `client/src/backend_client.cpp`

- [ ] **Step 1: 在 backend_client.cpp 末尾追加 report_position 实现**

```cpp
void BackendClient::report_position(
    const std::string& ride_no, int seq,
    double lat, double lng, int elapsed_sec) {
    // Backpressure:如果上一次还未发完,直接丢弃本次。
    bool expected = false;
    if (!pos_sending_.compare_exchange_strong(expected, true)) return;

    tutorial::ride_position_report req;
    req.set_ride_no(ride_no);
    req.set_seq(seq);
    req.set_lat(lat);
    req.set_lng(lng);
    req.set_elapsed_sec(elapsed_sec);

    // 在分离线程里走独立 socket 发送,不阻塞调用方
    std::thread([this, req] {
        try {
            asio::io_context ioc;
            asio::ip::tcp::socket socket(ioc);
            asio::ip::tcp::resolver r(ioc);
            asio::connect(socket, r.resolve(host_, std::to_string(port_)));
            bike::Frame frame{0x15, req.SerializeAsString()};
            auto bytes = bike::encode(frame);
            asio::write(socket, asio::buffer(bytes));
            // 不读响应(单向事件)
        } catch (...) {
            // 静默失败,下秒覆盖
        }
        pos_sending_.store(false);
    }).detach();
}
```

注意:`ride_position_report` 是单向事件(0x15,响应 0x16 未启用),`Frame.event_id` 设为 `0x15`,服务端 router 直接路由到 position_report handler。

- [ ] **Step 2: 完整编译**

Run: `cmake --build build --target bike_client`
Expected: 编译 + 链接通过。

- [ ] **Step 3: Commit**

```bash
git add client/src/backend_client.cpp
git commit -m "feat(client): implement 7 new BackendClient methods incl. async position report"
```

---

### Task 5: TrajectorySim 头文件

**Files:**
- Create: `client/src/trajectory_sim.hpp`

- [ ] **Step 1: 创建头文件**

```cpp
#pragma once

#include <QPointF>
#include <cstdint>

namespace bike::client {

// 本地模拟的骑行轨迹生成器。
// - 用 std::mt19937 + seed,deterministic(同 seed + 同起点 → 完全相同的轨迹)
// - 速度模型:8–15 km/h(典型共享单车)
// - 方向模型:每秒抖动 ±15°,保持大致直线
// - distance_m 用 Haversine 累加
class TrajectorySim {
public:
    explicit TrajectorySim(std::uint32_t seed);

    // 起点。reset 内部状态(distance_m = 0,seq = 0)。
    void start(double lat0, double lng0);

    // 推进一步(每秒调用一次)。返回新坐标点。
    // 调用前必须 start(),否则行为未定义。
    QPointF step();

    QPointF current() const { return current_; }
    double  distance_m() const { return distance_m_; }
    int     seq() const { return seq_; }

private:
    std::uint32_t seed_;
    double        speed_mps_;   // 当前速度,米/秒
    double        bearing_deg_; // 当前方位,0=北,顺时针
    QPointF       current_;
    double        distance_m_ = 0.0;
    int           seq_ = 0;
    bool          started_ = false;
};

} // namespace bike::client
```

- [ ] **Step 2: 暂不 commit,随 Task 6 一起。**

---

### Task 6: TrajectorySim 实现 + 单元测试(TDD)

**Files:**
- Create: `client/src/trajectory_sim.cpp`
- Create: `client/tests/test_trajectory_sim.cpp`

- [ ] **Step 1: 先写失败的测试**

`client/tests/test_trajectory_sim.cpp`:

```cpp
#include <gtest/gtest.h>
#include "trajectory_sim.hpp"

#include <cmath>
#include <vector>

using bike::client::TrajectorySim;

TEST(TrajectorySim, SameSeedSameTrajectory) {
    TrajectorySim a(42);
    a.start(39.9821, 116.3145);
    TrajectorySim b(42);
    b.start(39.9821, 116.3145);

    for (int i = 0; i < 60; ++i) {
        auto pa = a.step();
        auto pb = b.step();
        EXPECT_FLOAT_EQ(pa.x(), pb.x());
        EXPECT_FLOAT_EQ(pa.y(), pb.y());
    }
}

TEST(TrajectorySim, DistanceGrowsMonotonically) {
    TrajectorySim t(7);
    t.start(39.9821, 116.3145);
    double prev = 0.0;
    for (int i = 0; i < 60; ++i) {
        t.step();
        double d = t.distance_m();
        EXPECT_GE(d, prev - 1e-6);  // 允许浮点误差
        prev = d;
    }
}

TEST(TrajectorySim, OneMinuteDistanceInPlausibleRange) {
    // 60 秒 × 8–15 km/h × (1000/3600) m/s = 60 × [2.22, 4.17] = [133, 250]
    TrajectorySim t(123);
    t.start(39.9821, 116.3145);
    for (int i = 0; i < 60; ++i) t.step();
    double d = t.distance_m();
    EXPECT_GE(d, 100.0);
    EXPECT_LE(d, 280.0);
}

TEST(TrajectorySim, DifferentSeedDifferentTrajectory) {
    TrajectorySim a(1);
    a.start(39.9821, 116.3145);
    TrajectorySim b(2);
    b.start(39.9821, 116.3145);
    std::vector<QPointF> aa, bb;
    for (int i = 0; i < 10; ++i) { aa.push_back(a.step()); bb.push_back(b.step()); }
    bool any_diff = false;
    for (int i = 0; i < 10; ++i) {
        if (std::fabs(aa[i].x() - bb[i].x()) > 1e-9 ||
            std::fabs(aa[i].y() - bb[i].y()) > 1e-9) {
            any_diff = true; break;
        }
    }
    EXPECT_TRUE(any_diff);
}
```

- [ ] **Step 2: 运行测试,确认失败(类未实现)**

Run: `cmake --build build --target bike_client_tests && ctest --test-dir build -R bike_client_tests`
Expected: 失败,链接错误"undefined reference to TrajectorySim::TrajectorySim"等。

- [ ] **Step 3: 实现 trajectory_sim.cpp**

```cpp
#include "trajectory_sim.hpp"

#include <bike/geo.hpp>

#include <cmath>
#include <random>

namespace bike::client {

namespace {
constexpr double kMinSpeedKmh = 8.0;
constexpr double kMaxSpeedKmh = 15.0;
constexpr double kBearingJitterDeg = 15.0;
constexpr double kEarthRadiusM = 6371000.0;

// 给定起点、方位、距离,算新点。基于球面近似。
QPointF advance(double lat_deg, double lng_deg, double bearing_deg, double dist_m) {
    double lat1 = lat_deg * M_PI / 180.0;
    double lng1 = lng_deg * M_PI / 180.0;
    double brg  = bearing_deg * M_PI / 180.0;
    double dr   = dist_m / kEarthRadiusM;

    double lat2 = std::asin(std::sin(lat1) * std::cos(dr) +
                            std::cos(lat1) * std::sin(dr) * std::cos(brg));
    double lng2 = lng1 + std::atan2(std::sin(brg) * std::sin(dr) * std::cos(lat1),
                                    std::cos(dr) - std::sin(lat1) * std::sin(lat2));
    return QPointF(lat2 * 180.0 / M_PI, lng2 * 180.0 / M_PI);
}
} // namespace

TrajectorySim::TrajectorySim(std::uint32_t seed) : seed_(seed) {}

void TrajectorySim::start(double lat0, double lng0) {
    std::mt19937 rng(seed_);
    // 随机一个初始速度和方位
    std::uniform_real_distribution<double> spd(kMinSpeedKmh, kMaxSpeedKmh);
    std::uniform_real_distribution<double> brg(0.0, 360.0);
    speed_mps_   = spd(rng) * 1000.0 / 3600.0;
    bearing_deg_ = brg(rng);
    current_     = QPointF(lat0, lng0);
    distance_m_  = 0.0;
    seq_         = 0;
    started_     = true;
}

QPointF TrajectorySim::step() {
    if (!started_) return current_;

    // 每步抖动方向 ±15°,速度重新抽(模拟骑行起伏)
    std::mt19937 rng(seed_ + static_cast<std::uint32_t>(seq_) + 1);
    std::uniform_real_distribution<double> jit(-kBearingJitterDeg, kBearingJitterDeg);
    std::uniform_real_distribution<double> spd(kMinSpeedKmh, kMaxSpeedKmh);
    bearing_deg_ = std::fmod(bearing_deg_ + jit(rng) + 360.0, 360.0);
    speed_mps_   = spd(rng) * 1000.0 / 3600.0;

    QPointF next = advance(current_.x(), current_.y(), bearing_deg_, speed_mps_);

    // 用 Haversine 累加 distance(更精确,而非直接用 speed_mps_)
    double seg = bike::haversine_m(current_.x(), current_.y(), next.x(), next.y());
    distance_m_ += seg;
    current_ = next;
    ++seq_;
    return current_;
}

} // namespace bike::client
```

> **依赖说明**:`bike::haversine_m` 来自服务端 plan 的 Task 6(`common/include/bike/geo.hpp`)。若客户端在服务端 plan 完成前先开工,临时实现一个本地 haversine 即可:

```cpp
// 临时 fallback(若 bike::haversine_m 不存在):
namespace bike::client {
inline double local_haversine_m(double lat1, double lng1, double lat2, double lng2) {
    auto to_rad = [](double d) { return d * M_PI / 180.0; };
    double dlat = to_rad(lat2 - lat1);
    double dlng = to_rad(lng2 - lng1);
    double a = std::sin(dlat/2) * std::sin(dlat/2) +
               std::cos(to_rad(lat1)) * std::cos(to_rad(lat2)) *
               std::sin(dlng/2) * std::sin(dlng/2);
    return 2 * 6371000.0 * std::asin(std::sqrt(std::min(1.0, a)));
}
}
```

并替换 `bike::haversine_m(...)` 调用为 `local_haversine_m(...)`。

- [ ] **Step 4: 运行测试,确认全部通过**

Run: `cmake --build build --target bike_client_tests && ctest --test-dir build -R bike_client_tests --output-on-failure`
Expected: 4 个 case 全部 PASS。

- [ ] **Step 5: Commit**

```bash
git add client/src/trajectory_sim.hpp client/src/trajectory_sim.cpp client/tests/test_trajectory_sim.cpp
git commit -m "feat(client): add TrajectorySim with deterministic seeded trajectory + 4 unit tests"
```

---

### Task 7: LocationProvider(Qt Positioning 抽象)

**Files:**
- Create: `client/src/location_provider.hpp`
- Create: `client/src/location_provider.cpp`

- [ ] **Step 1: 创建头文件**

```cpp
#pragma once

#include <QObject>
#include <functional>

namespace bike::client {

// 用 Qt Positioning(QGeoPositionInfoSource)拿当前位置。
// 失败时回退到默认坐标(五道口中心)。
class LocationProvider : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(double lat, double lng)>;

    explicit LocationProvider(QObject* parent = nullptr);

    // 异步请求一次当前位置。回调在主线程触发。
    // 失败/超时(2s)→ 回调默认坐标(39.9821, 116.3145)。
    void request_once(Callback cb);

    static double default_lat() { return 39.9821; }
    static double default_lng() { return 116.3145; }
};

} // namespace bike::client
```

- [ ] **Step 2: 实现**

```cpp
#include "location_provider.hpp"

#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QTimer>
#include <QMetaObject>

namespace bike::client {

LocationProvider::LocationProvider(QObject* parent) : QObject(parent) {}

void LocationProvider::request_once(Callback cb) {
    auto* src = QGeoPositionInfoSource::createDefaultSource(this);
    if (!src) {
        cb(default_lat(), default_lng());
        return;
    }
    src->setUpdateInterval(0);  // 单次

    auto* timer = new QTimer(this);
    timer->setSingleShot(true);

    auto finalize = [cb, lat = default_lat(), lng = default_lng()]() {
        cb(lat, lng);
    };

    QObject::connect(src, &QGeoPositionInfoSource::positionUpdated,
                     this, [this, cb, src, timer](const QGeoPositionInfo& info) {
        timer->stop();
        if (info.isValid()) {
            auto coord = info.coordinate();
            cb(coord.latitude(), coord.longitude());
        } else {
            cb(default_lat(), default_lng());
        }
        src->deleteLater();
        timer->deleteLater();
    });

    QObject::connect(timer, &QTimer::timeout, this, [this, cb, src, timer]() {
        cb(default_lat(), default_lng());
        src->stopUpdates();
        src->deleteLater();
        timer->deleteLater();
    });

    timer->start(2000);
    src->requestUpdate(2000);
}

} // namespace bike::client
```

- [ ] **Step 3: 编译验证**

Run: `cmake --build build --target bike_client`
Expected: 编译通过(注意 AUTOMOC 已开启,LocationProvider 的 Q_OBJECT 会自动生成 moc)。

- [ ] **Step 4: Commit**

```bash
git add client/src/location_provider.hpp client/src/location_provider.cpp
git commit -m "feat(client): add LocationProvider wrapping Qt Positioning with default fallback"
```

---

### Task 8: 地图前端资源(HTML / JS / CSS)

**Files:**
- Create: `client/resources/map.html`
- Create: `client/resources/map.js`
- Create: `client/resources/map.css`

- [ ] **Step 1: 写 map.html**

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <title>Bike Map</title>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
    <script src="qrc:///qtwebchannel/qwebchannel.js"></script>
    <link rel="stylesheet" href="qrc:/map.css"/>
</head>
<body>
    <div id="map"></div>
    <script src="qrc:/map.js"></script>
</body>
</html>
```

- [ ] **Step 2: 写 map.css**

```css
html, body { margin: 0; padding: 0; height: 100%; }
#map { width: 100%; height: 100vh; }
.bike-icon-idle {
    background: #2563eb;
    width: 18px; height: 18px;
    border-radius: 50%;
    border: 2px solid #ffffff;
    box-shadow: 0 0 4px rgba(0,0,0,0.4);
}
.bike-icon-damaged {
    background: #dc2626;
    width: 18px; height: 18px;
    border-radius: 50%;
    border: 2px solid #ffffff;
    box-shadow: 0 0 4px rgba(0,0,0,0.4);
}
.user-icon {
    background: #10b981;
    width: 16px; height: 16px;
    border-radius: 50%;
    border: 3px solid #ffffff;
}
```

- [ ] **Step 3: 写 map.js**

```javascript
var map = null;
var userMarker = null;
var bikeMarkers = {};  // bike_no -> marker
var trajPolyline = null;
var trajMarkers = [];

new QWebChannel(qt.webChannelTransport, function(channel) {
    window.bridge = channel.objects.bridge;
    initMap();
});

function initMap() {
    map = L.map('map').setView([39.9821, 116.3145], 15);
    L.tileLayer('https://webrd0{s}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}', {
        subdomains: ['1','2','3','4'],
        maxZoom: 18,
        attribution: '© AMap'
    }).addTo(map);
}

// 由 C++ MapBridge 调用
function setUserLocation(lat, lng) {
    if (userMarker) map.removeLayer(userMarker);
    userMarker = L.marker([lat, lng], {
        icon: L.divIcon({className: 'user-icon', iconSize: [16, 16]})
    }).addTo(map);
}

function renderBikes(bikesJson) {
    // bikesJson: [{"bike_no":"BJ-000001","lat":39.98,"lng":116.31,"status":0}, ...]
    var bikes = JSON.parse(bikesJson);
    // 清掉旧的
    for (var no in bikeMarkers) {
        map.removeLayer(bikeMarkers[no]);
    }
    bikeMarkers = {};
    var idleCount = 0, damagedCount = 0;
    bikes.forEach(function(b) {
        var cls = b.status === 2 ? 'bike-icon-damaged' : 'bike-icon-idle';
        if (b.status === 2) damagedCount++; else idleCount++;
        var m = L.marker([b.lat, b.lng], {
            icon: L.divIcon({className: cls, iconSize: [18, 18]})
        }).addTo(map);
        m.bindTooltip(b.bike_no);
        m.on('click', function() {
            window.bridge.onBikeClicked(b.bike_no);
        });
        bikeMarkers[b.bike_no] = m;
    });
    if (window.bridge && window.bridge.bikeCountsUpdated) {
        window.bridge.bikeCountsUpdated(idleCount, damagedCount);
    }
}

function appendTrajectory(lat, lng) {
    if (!trajPolyline) {
        trajPolyline = L.polyline([[lat, lng]], {color: '#2563eb', weight: 4}).addTo(map);
    } else {
        trajPolyline.addLatLng([lat, lng]);
    }
    map.panTo([lat, lng], {animate: true});
}

function clearTrajectory() {
    if (trajPolyline) {
        map.removeLayer(trajPolyline);
        trajPolyline = null;
    }
}

function drawFullTrajectory(pointsJson) {
    // pointsJson: [{"lat":..,"lng":..,"elapsed_sec":..}, ...]
    clearTrajectory();
    var pts = JSON.parse(pointsJson).map(function(p) { return [p.lat, p.lng]; });
    trajPolyline = L.polyline(pts, {color: '#2563eb', weight: 4}).addTo(map);
    if (pts.length > 0) {
        map.fitBounds(trajPolyline.getBounds(), {padding: [40, 40]});
    }
}
```

- [ ] **Step 4: 暂不验证(需 Task 9 + Task 11 + MapView 一起)**

- [ ] **Step 5: Commit**

```bash
git add client/resources/map.html client/resources/map.js client/resources/map.css
git commit -m "feat(client): add Leaflet + AMap tile map frontend (html/js/css)"
```

---

### Task 9: 资源文件 map.qrc

**Files:**
- Create: `client/resources/map.qrc`

- [ ] **Step 1: 创建 .qrc**

```xml
<RCC>
    <qresource prefix="/">
        <file>map.html</file>
        <file>map.js</file>
        <file>map.css</file>
    </qresource>
</RCC>
```

- [ ] **Step 2: 验证 RCC 编译进二进制**

Run: `cmake --build build --target bike_client`
Expected: 编译通过,生成 `qrc_map.cpp`(自动)。

- [ ] **Step 3: Commit**

```bash
git add client/resources/map.qrc
git commit -m "build(client): add map.qrc for bundling map html/js/css"
```

---

### Task 10: MapBridge(C++ ↔ JS 桥接)

**Files:**
- Create: `client/src/map_bridge.hpp`
- Create: `client/src/map_bridge.cpp`

- [ ] **Step 1: 创建头文件**

```cpp
#pragma once

#include <QObject>
#include <QString>

namespace bike::client {

// 通过 QWebChannel 暴露给 map.js 的桥对象。
// JS 端调用 `window.bridge.onBikeClicked(bikeNo)` → 触发 bikeClicked 信号。
// C++ 端调用 `renderBikes(jsonStr)` → JS 端拿到字符串并渲染。
class MapBridge : public QObject {
    Q_OBJECT
public:
    explicit MapBridge(QObject* parent = nullptr);

signals:
    void bikeClicked(const QString& bike_no);
    void bikeCountsUpdated(int idle, int damaged);

public slots:
    // 由 JS 调用(map.js 中 `window.bridge.onBikeClicked(bike_no)`)
    void onBikeClicked(const QString& bike_no) { emit bikeClicked(bike_no); }
    void onBikeCountsUpdated(int idle, int damaged) { emit bikeCountsUpdated(idle, damaged); }
};

} // namespace bike::client
```

- [ ] **Step 2: 创建 .cpp(只有构造,因为 slots 都 inline)**

```cpp
#include "map_bridge.hpp"

namespace bike::client {

MapBridge::MapBridge(QObject* parent) : QObject(parent) {}

} // namespace bike::client
```

- [ ] **Step 3: 编译验证**

Run: `cmake --build build --target bike_client`
Expected: 编译通过。

- [ ] **Step 4: Commit**

```bash
git add client/src/map_bridge.hpp client/src/map_bridge.cpp
git commit -m "feat(client): add MapBridge QObject for QWebChannel C++↔JS bridge"
```

---

### Task 11: MapView(地图 tab)

**Files:**
- Create: `client/src/views/map_view.hpp`
- Create: `client/src/views/map_view.cpp`

- [ ] **Step 1: 创建头文件**

```cpp
#pragma once

#include "backend_client.hpp"
#include "location_provider.hpp"
#include "map_bridge.hpp"
#include "session_model.hpp"

#include <QWebEngineView>
#include <QWidget>

class QPushButton;
class QLabel;

namespace bike::client {

class MapView : public QWidget {
    Q_OBJECT
public:
    MapView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);

    void refresh();
    void set_status(const QString& s) { lb_status_->setText(s); }

    double my_lat() const { return my_lat_; }
    double my_lng() const { return my_lng_; }

signals:
    void unlockRequested(const QString& bike_no);  // 用户点了车 → 触发扫码
    void userLocationUpdated(double lat, double lng);

private slots:
    void onLoadFinished(bool ok);
    void onBikeClicked(const QString& bike_no);

private:
    BackendClient*   client_;
    SessionModel*    session_;
    QWebEngineView*  web_{nullptr};
    MapBridge*       bridge_{nullptr};
    LocationProvider loc_{nullptr};
    QPushButton*     btn_refresh_{nullptr};
    QPushButton*     btn_locate_{nullptr};
    QLabel*          lb_status_{nullptr};
    double           my_lat_ = LocationProvider::default_lat();
    double           my_lng_ = LocationProvider::default_lng();
};

} // namespace bike::client
```

- [ ] **Step 2: 创建 .cpp**

```cpp
#include "map_view.hpp"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QWebChannel>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

MapView::MapView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    web_ = new QWebEngineView(this);
    auto* channel = new QWebChannel(web_->page());
    bridge_ = new MapBridge(this);
    channel->registerObject(QStringLiteral("bridge"), bridge_);
    web_->page()->setWebChannel(channel);
    web_->load(QUrl("qrc:/map.html"));
    v->addWidget(web_, 1);

    auto* overlay = new QWidget(this);
    overlay->setObjectName("mapOverlay");
    auto* h = new QHBoxLayout(overlay);
    h->setContentsMargins(12, 12, 12, 12);
    btn_refresh_ = new QPushButton(QString::fromUtf8("刷新车辆"), overlay);
    btn_refresh_->setProperty("variant", "secondary");
    btn_locate_  = new QPushButton(QString::fromUtf8("定位"), overlay);
    btn_locate_->setProperty("variant", "secondary");
    lb_status_   = new QLabel(QString::fromUtf8("正在加载地图…"), overlay);
    lb_status_->setObjectName("status");
    h->addWidget(lb_status_);
    h->addStretch();
    h->addWidget(btn_locate_);
    h->addWidget(btn_refresh_);
    overlay->setMaximumHeight(56);
    v->addWidget(overlay);

    connect(web_, &QWebEngineView::loadFinished, this, &MapView::onLoadFinished);
    connect(bridge_, &MapBridge::bikeClicked, this, &MapView::onBikeClicked);
    connect(btn_refresh_, &QPushButton::clicked, this, &MapView::refresh);
    connect(btn_locate_, &QPushButton::clicked, this, [this] {
        loc_.request_once([this](double lat, double lng) {
            my_lat_ = lat; my_lng_ = lng;
            emit userLocationUpdated(lat, lng);
            QString js = QString("setUserLocation(%1, %2);").arg(lat).arg(lng);
            web_->page()->runJavaScript(js);
        });
    });
}

void MapView::onLoadFinished(bool ok) {
    if (!ok) {
        lb_status_->setText(QString::fromUtf8("地图加载失败"));
        return;
    }
    // 默认中心点
    QString js = QString("setUserLocation(%1, %2);")
                     .arg(LocationProvider::default_lat())
                     .arg(LocationProvider::default_lng());
    web_->page()->runJavaScript(js);
    refresh();
    loc_.request_once([this](double lat, double lng) {
        my_lat_ = lat; my_lng_ = lng;
        emit userLocationUpdated(lat, lng);
        QString js = QString("setUserLocation(%1, %2);").arg(lat).arg(lng);
        web_->page()->runJavaScript(js);
    });
}

void MapView::onBikeClicked(const QString& bike_no) {
    lb_status_->setText(QString::fromUtf8("已选车:%1").arg(bike_no));
    emit unlockRequested(bike_no);
}

void MapView::refresh() {
    if (!session_->logged_in()) {
        lb_status_->setText(QString::fromUtf8("尚未登录"));
        return;
    }
    lb_status_->setText(QString::fromUtf8("正在拉取附近车辆…"));
    double lat = my_lat_, lng = my_lng_;
    QtConcurrent::run([this, lat, lng] {
        try {
            auto rsp = client_->list_nearby_bikes(session_->token, lat, lng, 1000.0);
            QJsonArray arr;
            for (int i = 0; i < rsp.bikes_size(); ++i) {
                const auto& b = rsp.bikes(i);
                QJsonObject o;
                o["bike_no"] = QString::fromStdString(b.bike_no());
                o["lat"]     = b.lat();
                o["lng"]     = b.lng();
                o["status"]  = b.status();
                arr.append(o);
            }
            // 直接把 JSON 字面量嵌入 JS,无需再 quote
            QString jsonStr = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
            QMetaObject::invokeMethod(this, [this, jsonStr, n = rsp.bikes_size()] {
                web_->page()->runJavaScript(
                    QString("renderBikes(%1);").arg(jsonStr));
                lb_status_->setText(QString::fromUtf8("共 %1 辆车").arg(n));
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_status_->setText(QString::fromUtf8("加载失败:%1").arg(QString::fromStdString(msg)));
            });
        }
    });
}

} // namespace bike::client
```

- [ ] **Step 3: 编译验证**

Run: `cmake --build build --target bike_client`
Expected: 编译通过。

- [ ] **Step 4: Commit**

```bash
git add client/src/views/map_view.hpp client/src/views/map_view.cpp
git commit -m "feat(client): add MapView with QWebEngineView + QWebChannel + AMap tiles"
```

---

### Task 12: RideView(骑行中 tab)

**Files:**
- Create: `client/src/views/ride_view.hpp`
- Create: `client/src/views/ride_view.cpp`

- [ ] **Step 1: 创建头文件**

```cpp
#pragma once

#include "backend_client.hpp"
#include "session_model.hpp"
#include "trajectory_sim.hpp"

#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace bike::client {

class RideView : public QWidget {
    Q_OBJECT
public:
    RideView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);

    // 由 MainWindow 在 scan_unlock 成功后调用
    void start_ride(const QString& ride_no, double start_lat, double start_lng,
                    std::uint32_t sim_seed);
    void end_ride_external();  // 用户在 RideDetailDialog 触发结束(留作扩展,本期不用)

signals:
    void ended(const QString& ride_no, int amount_cent, int balance_after);
    void positionUpdated(double lat, double lng);  // 给地图叠加层用

private slots:
    void on_tick();
    void on_end_clicked();

private:
    BackendClient* client_;
    SessionModel*  session_;
    QLabel*  lb_title_{nullptr};
    QLabel*  lb_timer_{nullptr};
    QLabel*  lb_distance_{nullptr};
    QLabel*  lb_estimate_{nullptr};
    QLabel*  lb_status_{nullptr};
    QPushButton* btn_end_{nullptr};

    QTimer         timer_;
    TrajectorySim  sim_;
    QString        ride_no_;
    double         start_lat_ = 0.0;
    double         start_lng_ = 0.0;
    int            elapsed_sec_ = 0;
    bool           active_ = false;
};

} // namespace bike::client
```

- [ ] **Step 2: 创建 .cpp**

```cpp
#include "ride_view.hpp"

#include <chrono>
#include <QHBoxLayout>
#include <QString>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

namespace {
QString format_hms(int sec) {
    int h = sec / 3600;
    int m = (sec / 60) % 60;
    int s = sec % 60;
    return QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

int estimate_fee_cent(int sec) {
    constexpr int base = 15 * 60;
    constexpr int step = 15 * 60;
    if (sec <= base) return 100;
    int extra = sec - base;
    int chunks = (extra + step - 1) / step;
    return 100 + chunks * 50;
}
} // namespace

RideView::RideView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session), sim_(0) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(10);

    lb_title_ = new QLabel(QString::fromUtf8("尚未开始骑行"), this);
    lb_title_->setObjectName("title");
    v->addWidget(lb_title_);

    lb_timer_ = new QLabel("00:00:00", this);
    lb_timer_->setObjectName("balance");
    v->addWidget(lb_timer_);

    lb_distance_ = new QLabel(QString::fromUtf8("📏 0.00 km    💰 约 ¥ 1.00"), this);
    lb_distance_->setObjectName("subtitle");
    v->addWidget(lb_distance_);

    lb_estimate_ = new QLabel(this);
    lb_estimate_->setObjectName("status");
    v->addWidget(lb_estimate_);

    v->addStretch();

    btn_end_ = new QPushButton(QString::fromUtf8("结束骑行"), this);
    btn_end_->setMinimumHeight(44);
    btn_end_->setStyleSheet("background:#dc2626;");
    btn_end_->setEnabled(false);
    v->addWidget(btn_end_);

    lb_status_ = new QLabel(this);
    lb_status_->setObjectName("status");
    v->addWidget(lb_status_);

    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, &RideView::on_tick);
    connect(btn_end_, &QPushButton::clicked, this, &RideView::on_end_clicked);
}

void RideView::start_ride(const QString& ride_no, double start_lat, double start_lng,
                          std::uint32_t sim_seed) {
    ride_no_   = ride_no;
    start_lat_ = start_lat;
    start_lng_ = start_lng;
    elapsed_sec_ = 0;
    active_ = true;
    sim_ = TrajectorySim(sim_seed);
    sim_.start(start_lat, start_lng);

    lb_title_->setText(QString::fromUtf8("骑行中  %1").arg(ride_no));
    lb_timer_->setText("00:00:00");
    lb_distance_->setText(QString::fromUtf8("📏 0.00 km    💰 约 ¥ 1.00"));
    btn_end_->setEnabled(true);
    lb_status_->setText(QString());

    timer_.start();
}

void RideView::on_tick() {
    if (!active_) return;
    ++elapsed_sec_;
    auto p = sim_.step();
    emit positionUpdated(p.x(), p.y());

    // 异步上报位置(独立 socket,带 backpressure)
    client_->report_position(ride_no_.toStdString(), sim_.seq(),
                             p.x(), p.y(), elapsed_sec_);

    lb_timer_->setText(format_hms(elapsed_sec_));
    double km = sim_.distance_m() / 1000.0;
    int fee = estimate_fee_cent(elapsed_sec_);
    lb_distance_->setText(QString::fromUtf8("📏 %L1 km    💰 约 ¥ %L2")
                              .arg(km, 0, 'f', 2)
                              .arg(fee / 100.0, 0, 'f', 2));
}

void RideView::on_end_clicked() {
    if (!active_) return;
    btn_end_->setEnabled(false);
    lb_status_->setText(QString::fromUtf8("正在结束骑行…"));
    timer_.stop();

    double end_lat = sim_.current().x();
    double end_lng = sim_.current().y();
    QString ride_no = ride_no_;

    QtConcurrent::run([this, ride_no, end_lat, end_lng] {
        try {
            auto rsp = client_->end_ride(session_->token, ride_no.toStdString(),
                                         end_lat, end_lng);
            QMetaObject::invokeMethod(this, [this, rsp] {
                active_ = false;
                lb_title_->setText(QString::fromUtf8("骑行已结束"));
                lb_status_->setText(QString::fromUtf8("消费 ¥ %L1,余额 ¥ %L2")
                                        .arg(rsp.amount_cent() / 100.0, 0, 'f', 2)
                                        .arg(rsp.balance_after() / 100.0, 0, 'f', 2));
                emit ended(QString::fromStdString(rsp.desc().empty() ? "" : rsp.desc()),
                           rsp.amount_cent(), rsp.balance_after());
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_status_->setText(QString::fromUtf8("结束失败:%1,请重试").arg(QString::fromStdString(msg)));
                btn_end_->setEnabled(true);
                timer_.start();  // 允许重试,继续计时
            });
        }
    });
}

} // namespace bike::client
```

- [ ] **Step 3: 编译验证**

Run: `cmake --build build --target bike_client`
Expected: 编译通过。

- [ ] **Step 4: Commit**

```bash
git add client/src/views/ride_view.hpp client/src/views/ride_view.cpp
git commit -m "feat(client): add RideView with 1Hz trajectory sim + async position report"
```

---

### Task 13: RideHistoryView(历史 tab)

**Files:**
- Create: `client/src/views/ride_history_view.hpp`
- Create: `client/src/views/ride_history_view.cpp`

- [ ] **Step 1: 创建头文件**

```cpp
#pragma once

#include "backend_client.hpp"
#include "session_model.hpp"

#include <QPushButton>
#include <QTableWidget>
#include <QWidget>
#include <QLabel>

namespace bike::client {

class RideHistoryView : public QWidget {
    Q_OBJECT
public:
    RideHistoryView(BackendClient* client, SessionModel* session, QWidget* parent = nullptr);

    void refresh();

signals:
    void viewDetailRequested(const QString& ride_no);

private slots:
    void on_row_double_clicked(int row);

private:
    BackendClient* client_;
    SessionModel*  session_;
    QTableWidget*  table_{nullptr};
    QPushButton*   btn_refresh_{nullptr};
    QLabel*        lb_status_{nullptr};
};

} // namespace bike::client
```

- [ ] **Step 2: 创建 .cpp**

```cpp
#include "ride_history_view.hpp"

#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

RideHistoryView::RideHistoryView(BackendClient* client, SessionModel* session, QWidget* parent)
    : QWidget(parent), client_(client), session_(session) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(10);

    auto* title = new QLabel(QString::fromUtf8("历史骑行"), this);
    title->setObjectName("title");
    v->addWidget(title);
    auto* sub = new QLabel(QString::fromUtf8("双击任一行查看完整轨迹"), this);
    sub->setObjectName("subtitle");
    v->addWidget(sub);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({
        QString::fromUtf8("订单号"),
        QString::fromUtf8("起始时间"),
        QString::fromUtf8("距离 / 时长"),
        QString::fromUtf8("金额"),
    });
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->setMinimumHeight(320);
    v->addWidget(table_);

    auto* h = new QHBoxLayout;
    btn_refresh_ = new QPushButton(QString::fromUtf8("刷新"), this);
    btn_refresh_->setProperty("variant", "secondary");
    h->addWidget(btn_refresh_);
    h->addStretch();
    v->addLayout(h);

    lb_status_ = new QLabel(this);
    lb_status_->setObjectName("status");
    v->addWidget(lb_status_);

    connect(btn_refresh_, &QPushButton::clicked, this, &RideHistoryView::refresh);
    connect(table_, &QTableWidget::cellDoubleClicked,
            this, &RideHistoryView::on_row_double_clicked);
}

void RideHistoryView::refresh() {
    if (!session_->logged_in()) {
        lb_status_->setText(QString::fromUtf8("尚未登录"));
        return;
    }
    lb_status_->setText(QString::fromUtf8("正在加载历史…"));
    QtConcurrent::run([this] {
        try {
            auto rsp = client_->list_rides(session_->token, 50);
            QMetaObject::invokeMethod(this, [this, rsp] {
                table_->setRowCount(rsp.rides_size());
                for (int i = 0; i < rsp.rides_size(); ++i) {
                    const auto& r = rsp.rides(i);
                    auto* it_no = new QTableWidgetItem(QString::fromStdString(r.ride_no()));
                    auto* it_tm = new QTableWidgetItem(QString::fromStdString(r.start_tm()));
                    auto* it_ds = new QTableWidgetItem(QString::fromUtf8("%1 km / %2 min")
                        .arg(r.distance_m() / 1000.0, 0, 'f', 2)
                        .arg((r.duration_sec() + 30) / 60));
                    auto* it_amt = new QTableWidgetItem(QString("¥ %L1")
                        .arg(r.amount_cent() / 100.0, 0, 'f', 2));
                    it_amt->setForeground(QColor("#dc2626"));
                    table_->setItem(i, 0, it_no);
                    table_->setItem(i, 1, it_tm);
                    table_->setItem(i, 2, it_ds);
                    table_->setItem(i, 3, it_amt);
                }
                lb_status_->setText(QString::fromUtf8("共 %1 条").arg(rsp.rides_size()));
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_status_->setText(QString::fromUtf8("加载失败:%1").arg(QString::fromStdString(msg)));
            });
        }
    });
}

void RideHistoryView::on_row_double_clicked(int row) {
    auto* it = table_->item(row, 0);
    if (!it) return;
    emit viewDetailRequested(it->text());
}

} // namespace bike::client
```

- [ ] **Step 3: 编译验证**

Run: `cmake --build build --target bike_client`
Expected: 编译通过。

- [ ] **Step 4: Commit**

```bash
git add client/src/views/ride_history_view.hpp client/src/views/ride_history_view.cpp
git commit -m "feat(client): add RideHistoryView table + double-click to view detail"
```

---

### Task 14: RideDetailDialog(轨迹回放对话框)

**Files:**
- Create: `client/src/views/ride_detail_dialog.hpp`
- Create: `client/src/views/ride_detail_dialog.cpp`

- [ ] **Step 1: 创建头文件**

```cpp
#pragma once

#include "backend_client.hpp"
#include "map_bridge.hpp"

#include <QDialog>
#include <QWebEngineView>
#include <QTimer>
#include <QLabel>
#include <QPushButton>

namespace bike::client {

class RideDetailDialog : public QDialog {
    Q_OBJECT
public:
    RideDetailDialog(BackendClient* client, const QString& token,
                     const QString& ride_no, QWidget* parent = nullptr);

private slots:
    void on_detail_loaded();
    void on_play_pause();
    void on_replay_tick();

private:
    BackendClient*  client_;
    QString         token_;
    QString         ride_no_;
    QWebEngineView* web_{nullptr};
    MapBridge*      bridge_{nullptr};
    QLabel*         lb_meta_{nullptr};
    QPushButton*    btn_play_{nullptr};
    QLabel*         lb_progress_{nullptr};
    QTimer          timer_;

    tutorial::get_ride_detail_response detail_;
    int                replay_idx_ = 0;
    bool               playing_ = false;
};

} // namespace bike::client
```

- [ ] **Step 2: 创建 .cpp**

```cpp
#include "ride_detail_dialog.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QWebChannel>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

namespace {
QString format_hms(int sec) {
    int h = sec / 3600;
    int m = (sec / 60) % 60;
    int s = sec % 60;
    return QString("%1:%2:%3").arg(h,2,10,QChar('0')).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
}
} // namespace

RideDetailDialog::RideDetailDialog(BackendClient* client, const QString& token,
                                   const QString& ride_no, QWidget* parent)
    : QDialog(parent), client_(client), token_(token), ride_no_(ride_no) {
    setWindowTitle(QString::fromUtf8("订单 %1").arg(ride_no));
    resize(720, 600);

    auto* v = new QVBoxLayout(this);

    lb_meta_ = new QLabel(QString::fromUtf8("正在加载…"), this);
    lb_meta_->setObjectName("subtitle");
    v->addWidget(lb_meta_);

    web_ = new QWebEngineView(this);
    auto* channel = new QWebChannel(web_->page());
    bridge_ = new MapBridge(this);
    channel->registerObject(QStringLiteral("bridge"), bridge_);
    web_->page()->setWebChannel(channel);
    web_->load(QUrl("qrc:/map.html"));
    v->addWidget(web_, 1);

    auto* h = new QHBoxLayout;
    btn_play_ = new QPushButton(QString::fromUtf8("▶ 回放"), this);
    btn_play_->setEnabled(false);
    auto* btn_close = new QPushButton(QString::fromUtf8("关闭"), this);
    btn_close->setProperty("variant", "secondary");
    lb_progress_ = new QLabel("00:00 / 00:00", this);
    lb_progress_->setObjectName("status");
    h->addWidget(btn_play_);
    h->addWidget(lb_progress_);
    h->addStretch();
    h->addWidget(btn_close);
    v->addLayout(h);

    connect(btn_close, &QPushButton::clicked, this, &QDialog::reject);
    connect(btn_play_, &QPushButton::clicked, this, &RideDetailDialog::on_play_pause);
    connect(&timer_, &QTimer::timeout, this, &RideDetailDialog::on_replay_tick);
    timer_.setInterval(50);  // 20fps 让小圆点平滑

    // 异步拉详情
    QtConcurrent::run([this] {
        try {
            auto rsp = client_->get_ride_detail(token_.toStdString(), ride_no_.toStdString());
            QMetaObject::invokeMethod(this, [this, rsp] {
                detail_ = rsp;
                on_detail_loaded();
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                lb_meta_->setText(QString::fromUtf8("加载失败:%1").arg(QString::fromStdString(msg)));
            });
        }
    });
}

void RideDetailDialog::on_detail_loaded() {
    if (detail_.points_size() == 0) {
        lb_meta_->setText(QString::fromUtf8("此订单无轨迹数据"));
        return;
    }
    QJsonArray arr;
    for (int i = 0; i < detail_.points_size(); ++i) {
        const auto& p = detail_.points(i);
        QJsonObject o;
        o["lat"] = p.lat();
        o["lng"] = p.lng();
        o["elapsed_sec"] = p.elapsed_sec();
        arr.append(o);
    }
    QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));

    int dur = detail_.duration_sec();
    double km = detail_.distance_m() / 1000.0;
    double yuan = detail_.amount_cent() / 100.0;
    lb_meta_->setText(QString::fromUtf8("时长 %1   距离 %L2 km   费用 ¥ %L3")
                          .arg(format_hms(dur))
                          .arg(km, 0, 'f', 2)
                          .arg(yuan, 0, 'f', 2));

    // 等 web_ 加载完再 runJavaScript
    QMetaObject::invokeMethod(this, [this, json] {
        web_->page()->runJavaScript(QString("drawFullTrajectory(%1);").arg(json));
        lb_progress_->setText(QString("00:00 / %1").arg(format_hms(dur)));
        btn_play_->setEnabled(true);
    }, Qt::QueuedConnection);
}

void RideDetailDialog::on_play_pause() {
    if (detail_.points_size() == 0) return;
    playing_ = !playing_;
    btn_play_->setText(playing_ ? QString::fromUtf8("⏸ 暂停") : QString::fromUtf8("▶ 回放"));
    if (playing_) {
        if (replay_idx_ >= detail_.points_size()) replay_idx_ = 0;
        timer_.start();
    } else {
        timer_.stop();
    }
}

void RideDetailDialog::on_replay_tick() {
    if (replay_idx_ >= detail_.points_size()) {
        timer_.stop();
        playing_ = false;
        btn_play_->setText(QString::fromUtf8("▶ 回放"));
        return;
    }
    const auto& p = detail_.points(replay_idx_);
    web_->page()->runJavaScript(
        QString("appendTrajectory(%1, %2);").arg(p.lat()).arg(p.lng()));
    lb_progress_->setText(QString("%1 / %2")
        .arg(format_hms(p.elapsed_sec()))
        .arg(format_hms(detail_.duration_sec())));
    ++replay_idx_;
}

} // namespace bike::client
```

- [ ] **Step 3: 编译验证**

Run: `cmake --build build --target bike_client`
Expected: 编译通过。

- [ ] **Step 4: Commit**

```bash
git add client/src/views/ride_detail_dialog.hpp client/src/views/ride_detail_dialog.cpp
git commit -m "feat(client): add RideDetailDialog with full-trajectory + 20fps replay"
```

---

### Task 15: MainWindow 重构(QStackedWidget + 5 tab + QSettings 恢复)

**Files:**
- Modify: `client/src/mainwindow.hpp`
- Modify: `client/src/mainwindow.cpp`

- [ ] **Step 1: 重写 mainwindow.hpp**

```cpp
#pragma once

#include <QMainWindow>
#include <QSettings>
#include <QStackedWidget>
#include <QString>

#include "backend_client.hpp"
#include "session_model.hpp"
#include "views/login_view.hpp"
#include "views/wallet_view.hpp"
#include "views/records_view.hpp"
#include "views/map_view.hpp"
#include "views/ride_view.hpp"
#include "views/ride_history_view.hpp"
#include "views/ride_detail_dialog.hpp"

class QTabWidget;

namespace bike::client {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void on_logged_in();
    void on_unlock_requested(const QString& bike_no);
    void on_ride_ended(const QString& desc, int amount_cent, int balance_after);
    void on_view_detail(const QString& ride_no);

private:
    void enter_main_ui();
    void check_orphan_ride();

    BackendClient client_;
    SessionModel  session_;
    QStackedWidget* stack_{nullptr};
    QTabWidget*     tabs_{nullptr};
    LoginView*        login_{nullptr};
    MapView*          map_view_{nullptr};
    RideView*         ride_view_{nullptr};
    WalletView*       wallet_{nullptr};
    RecordsView*      records_{nullptr};
    RideHistoryView*  history_{nullptr};

    QString active_ride_;  // 从 QSettings 读出来,扫描成功后写,结束后清
};

} // namespace bike::client
```

- [ ] **Step 2: 重写 mainwindow.cpp(保留原 stylesheet,改造构造函数)**

```cpp
#include "mainwindow.hpp"

#include <QDateTime>
#include <QFont>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTabWidget>
#include <QtConcurrent/QtConcurrent>

namespace bike::client {

namespace {

constexpr const char* kStyle = R"(
* {
    font-family: 'Microsoft YaHei UI', 'Segoe UI', sans-serif;
    font-size: 13px;
    color: #1f2937;
}

QMainWindow, QWidget {
    background: #f7f8fa;
}

QStackedWidget { background: #f7f8fa; }

QTabWidget::pane {
    border: 1px solid #e5e7eb;
    border-radius: 8px;
    top: -1px;
    background: #ffffff;
    padding: 16px;
}

QTabBar::tab {
    background: #eef0f3;
    color: #4b5563;
    padding: 10px 24px;
    margin-right: 4px;
    border-top-left-radius: 8px;
    border-top-right-radius: 8px;
    min-width: 110px;
    font-weight: 500;
}

QTabBar::tab:selected {
    background: #ffffff;
    color: #2563eb;
    border: 1px solid #e5e7eb;
    border-bottom: 2px solid #2563eb;
}

QTabBar::tab:hover:!selected { background: #e3e6ea; }
QTabBar::tab:disabled { color: #9ca3af; background: #f3f4f6; }

QLabel#title { font-size: 22px; font-weight: 600; color: #111827; padding: 4px 0 12px 0; }
QLabel#subtitle { font-size: 12px; color: #6b7280; padding-bottom: 16px; }
QLabel#balance { font-size: 32px; font-weight: 700; color: #2563eb; padding: 8px 0; }
QLabel#status { color: #6b7280; font-size: 12px; padding: 4px 0; }
QLabel#status[err="true"] { color: #dc2626; }
QLabel#status[ok="true"]  { color: #059669; }

QLineEdit {
    padding: 8px 12px;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    background: #ffffff;
    selection-background-color: #2563eb;
}
QLineEdit:focus { border: 1px solid #2563eb; }

QPushButton {
    background: #2563eb;
    color: #ffffff;
    border: none;
    border-radius: 6px;
    padding: 8px 20px;
    font-weight: 500;
}
QPushButton:hover { background: #1d4ed8; }
QPushButton:pressed { background: #1e40af; }
QPushButton:disabled { background: #9ca3af; }
QPushButton[variant="secondary"] {
    background: #ffffff;
    color: #2563eb;
    border: 1px solid #2563eb;
}
QPushButton[variant="secondary"]:hover { background: #eff6ff; }

QHeaderView::section {
    background: #f3f4f6;
    color: #374151;
    padding: 8px;
    border: none;
    border-bottom: 1px solid #e5e7eb;
    font-weight: 600;
}

QTableWidget {
    background: #ffffff;
    border: 1px solid #e5e7eb;
    border-radius: 6px;
    gridline-color: #f3f4f6;
    selection-background-color: #dbeafe;
    selection-color: #1e40af;
}
QTableWidget::item { padding: 6px 8px; }
QTableWidget::item:selected { background: #dbeafe; color: #1e40af; }

QWidget#mapOverlay { background: rgba(255,255,255,0.9); border-top: 1px solid #e5e7eb; }
)";

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), client_("124.220.92.243", 8888) {
    setStyleSheet(QString::fromUtf8(kStyle));

    stack_ = new QStackedWidget(this);
    login_     = new LoginView(&client_, &session_, stack_);
    stack_->addWidget(login_);
    setCentralWidget(stack_);

    // 子 view 延迟到登录成功后再实例化(避免 backend_client 在未登录时调)

    connect(login_, &LoginView::logged_in, this, &MainWindow::on_logged_in);

    setWindowTitle(QString::fromUtf8("共享单车 · 用户客户端"));
    resize(960, 640);
}

void MainWindow::enter_main_ui() {
    if (tabs_) return;  // 已构造过

    tabs_       = new QTabWidget(stack_);
    map_view_   = new MapView(&client_, &session_, tabs_);
    ride_view_  = new RideView(&client_, &session_, tabs_);
    wallet_     = new WalletView(&client_, &session_, tabs_);
    records_    = new RecordsView(&client_, &session_, tabs_);
    history_    = new RideHistoryView(&client_, &session_, tabs_);

    tabs_->addTab(map_view_,  QString::fromUtf8("地图"));
    tabs_->addTab(ride_view_, QString::fromUtf8("骑行中"));
    tabs_->addTab(wallet_,    QString::fromUtf8("钱包"));
    tabs_->addTab(records_,   QString::fromUtf8("账单"));
    tabs_->addTab(history_,   QString::fromUtf8("历史"));

    // 骑行 tab 默认禁用,扫码成功后启用
    tabs_->setTabEnabled(1, false);

    stack_->addWidget(tabs_);
    stack_->setCurrentWidget(tabs_);

    // 连接信号
    connect(map_view_, &MapView::unlockRequested,
            this, &MainWindow::on_unlock_requested);
    connect(ride_view_, &RideView::ended,
            this, &MainWindow::on_ride_ended);
    connect(history_, &RideHistoryView::viewDetailRequested,
            this, &MainWindow::on_view_detail);

    // 各 tab 加载初始数据
    wallet_->refresh_balance();
    records_->refresh();
    history_->refresh();

    check_orphan_ride();
}

void MainWindow::on_logged_in() {
    enter_main_ui();
}

void MainWindow::on_unlock_requested(const QString& bike_no) {
    if (!session_.logged_in()) return;
    double lat = map_view_->my_lat();
    double lng = map_view_->my_lng();

    map_view_->set_status(QString::fromUtf8("正在解锁 %1…").arg(bike_no));
    QtConcurrent::run([this, bike_no, lat, lng] {
        try {
            auto rsp = client_.scan_unlock(session_.token, bike_no.toStdString(), lat, lng);
            QMetaObject::invokeMethod(this, [this, rsp, bike_no, lat, lng] {
                if (rsp.code() != 200) {
                    QMessageBox::warning(this, QString::fromUtf8("解锁失败"),
                        QString::fromUtf8("解锁失败:%1").arg(QString::fromStdString(rsp.desc())));
                    return;
                }
                QString ride_no = QString::fromStdString(rsp.ride_no());
                active_ride_ = ride_no;
                QSettings().setValue("active_ride", ride_no);

                // 启动骑行 view
                std::uint32_t seed = static_cast<std::uint32_t>(QDateTime::currentSecsSinceEpoch());
                ride_view_->start_ride(ride_no, lat, lng, seed);
                tabs_->setTabEnabled(1, true);
                tabs_->setCurrentIndex(1);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                QMessageBox::critical(this, QString::fromUtf8("网络异常"),
                    QString::fromUtf8("解锁异常:%1").arg(QString::fromStdString(msg)));
            });
        }
    });
}

void MainWindow::on_ride_ended(const QString&, int amount_cent, int) {
    active_ride_.clear();
    QSettings().remove("active_ride");

    tabs_->setTabEnabled(1, false);
    tabs_->setCurrentIndex(3);  // 跳到账单
    records_->refresh();
    wallet_->refresh_balance();

    QMessageBox::information(this, QString::fromUtf8("骑行结束"),
        QString::fromUtf8("本次消费 ¥ %L1").arg(amount_cent / 100.0, 0, 'f', 2));
}

void MainWindow::on_view_detail(const QString& ride_no) {
    auto* dlg = new RideDetailDialog(&client_, QString::fromStdString(session_.token),
                                     ride_no, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModal(true);
    dlg->show();
}

void MainWindow::check_orphan_ride() {
    QString ride_no = QSettings().value("active_ride").toString();
    if (ride_no.isEmpty()) return;

    auto ret = QMessageBox::question(this,
        QString::fromUtf8("检测到未结订单"),
        QString::fromUtf8("订单 %1 上次未结束,是否尝试结束?\n(若服务端已重启,此订单已失效)").arg(ride_no),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        QSettings().remove("active_ride");
        return;
    }

    // 发 end_ride 兜底:服务端识别 session 不存在 → 走幂等历史路径或返回 404
    QtConcurrent::run([this, ride_no] {
        try {
            auto rsp = client_.end_ride(session_.token, ride_no.toStdString(),
                                        39.9821, 116.3145);
            QMetaObject::invokeMethod(this, [this, rsp] {
                QSettings().remove("active_ride");
                QMessageBox::information(this, QString::fromUtf8("已结算"),
                    QString::fromUtf8("订单已结束,消费 ¥ %L1").arg(rsp.amount_cent() / 100.0, 0, 'f', 2));
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, msg = std::string(e.what())] {
                QSettings().remove("active_ride");
                QMessageBox::warning(this, QString::fromUtf8("订单已失效"),
                    QString::fromUtf8("无法结算:%1\n已清理本地状态。").arg(QString::fromStdString(msg)));
            });
        }
    });
}

} // namespace bike::client
```

- [ ] **Step 3: 编译 + 启动跑通登录**

Run:
```bash
cmake --build build --target bike_client
./build/bike_client   # (或 Windows: build/Debug/bike_client.exe)
```

Expected: 启动 → 登录页 → 输入手机号 → 拿验证码 → 登录成功 → 跳到 5-tab 主界面,地图加载出周围车。

- [ ] **Step 4: Commit**

```bash
git add client/src/mainwindow.hpp client/src/mainwindow.cpp
git commit -m "feat(client): restructure MainWindow to QStackedWidget + 5 tabs + QSettings recovery"
```

---

### Task 16: 手动 smoke 清单

**Files:**
- Create: `docs/smoke_checklist.md`

- [ ] **Step 1: 写清单**

```markdown
# 客户端 Smoke 清单

> 每次发版前,连接到测试服(124.220.92.243:8888),按此清单跑一遍。
> 所有项目都应能正常完成,任意一项失败即阻断发版。

## 准备
- [ ] 服务端已起,Docker compose healthy
- [ ] MySQL 60 辆 seed bike 已就位
- [ ] 至少一个测试账户余额 ≥ ¥ 5(可手动 SQL UPDATE 或走充值)

## 1. 登录 + 钱包
- [ ] 启动客户端 → 登录页正常显示
- [ ] 输入手机号 15600000010 → 点"获取验证码" → 6 位验证码回显在 status 里
- [ ] 输入验证码 → 点"登录" → 跳到主界面
- [ ] 钱包 tab 余额正确(¥ X.XX)

## 2. 地图
- [ ] 地图 tab 自动加载 → 显示五道口附近
- [ ] 至少显示 5 个蓝色车点(idle)
- [ ] 至少 1 个红色车点(damaged)
- [ ] 点"定位"按钮 → 用户圆点跳到当前位置(或默认坐标)
- [ ] 点"刷新车辆" → 车点重新渲染

## 3. 扫码解锁
- [ ] 点一个蓝色车点 → 底部 status 显示"已选车:BJ-000xxx"
- [ ] 等待 scan_unlock 成功 → 跳到骑行 tab,骑行 tab 启用
- [ ] 标题显示"骑行中  R20260719xxxxxx"
- [ ] 计时器每秒 +1,距离 + 估算费用实时更新

## 4. 骑行中
- [ ] 骑行 ≥ 30 秒(超出起步价时长)
- [ ] 估算费用从 ¥ 1.00 跳到 ¥ 1.50
- [ ] 关掉客户端 → 重新启动 → 弹"检测到未结订单"对话框
- [ ] 点"是" → 服务端返回订单失效(404)→ 客户端清 QSettings,提示"订单已失效"

## 5. 结束骑行
- [ ] 点"结束骑行" → 跳到账单 tab,弹"本次消费 ¥ X.XX"提示
- [ ] 钱包余额更新(已扣款)
- [ ] 账单表多一条消费记录,金额标红

## 6. 历史 + 回放
- [ ] 历史 tab 自动刷新 → 显示刚结束的订单
- [ ] 双击订单行 → 弹 RideDetailDialog
- [ ] 地图绘制完整 polyline
- [ ] 点"▶ 回放" → 小圆点沿轨迹移动,可暂停

## 7. 报修
- [ ] 回地图 tab → 点红色 damaged 车 → 状态显示"已选车"
- [ ] (后续版本:专门有报修入口;本期可在扫码时验证 409)
- [ ] 试图扫码 BJ-000058(damaged) → 弹"解锁失败:车辆故障"

## 8. 异常网络
- [ ] 关掉服务端 → 客户端任意操作 → 弹"网络异常"
- [ ] 重启服务端 → 客户端操作恢复正常

---
完成全部清单后,在 PR 描述里附上截图/录屏。
```

- [ ] **Step 2: Commit**

```bash
git add docs/smoke_checklist.md
git commit -m "docs: add manual smoke checklist for client release gate"
```

---

## Self-Review Checklist

### Spec 覆盖

| Spec 章节 | 由哪个 Task 实现 |
|---|---|
| 4 客户端 UX(主窗口结构) | Task 15 |
| 4 MapView | Task 8/9/10/11 |
| 4 RideView | Task 12 |
| 4 RideHistoryView | Task 13 |
| 4 RideDetailDialog | Task 14 |
| 5.1 Protobuf 新消息 | 服务端 plan Task 1(本 plan 复用) |
| 6.6 BackendClient 改造(7 方法) | Task 2/3/4 |
| 6.7 TrajectorySim | Task 5/6 |
| 7.2 客户端并发(pos_socket 独立 + backpressure) | Task 4 |
| 7.6 客户端崩溃恢复(QSettings) | Task 15 |
| 8.7 不测 Qt UI,人工 smoke | Task 16 |
| 8.8 smoke 清单 | Task 16 |

### 已知简化 / TODO(下次迭代再做)

1. **报修入口 UI** —— 本期 MVP 只验证扫码 damaged 车返回 409;独立报修按钮(ReportDamageView)留作下版
2. **Leaflet CSS/JS 走 unpkg CDN** —— 离线场景需要把 leaflet.css/leaflet.js 也打进 map.qrc(本期联网环境够用)
3. **AMap 瓦片需联网** —— 同上,离线部署时换本地瓦片或离线 mbtiles

### 类型一致性

- `BackendClient::scan_unlock` 在 Task 2 声明、Task 3 实现、Task 15 调用,签名一致:`(token, bike_no, lat, lng) → scan_unlock_response`
- `TrajectorySim::start/step/current/distance_m/seq` 在 Task 5 声明、Task 6 实现 + 测试、Task 12 调用,签名一致
- `MapView::unlockRequested(bike_no)` 在 Task 11 定义、Task 15 接收,信号参数一致
- `RideView::ended(desc, amount_cent, balance_after)` 在 Task 12 定义、Task 15 接收,签名一致(注意:Task 12 的 emit 传的第一个参数是 rsp.desc() 的空字符串兜底,Task 15 的 slot 忽略之)
- `RideHistoryView::viewDetailRequested(ride_no)` 在 Task 13 定义、Task 15 接收,一致

### 占位扫描

无 TBD / TODO / "实现略" 等占位。每个步骤都给了完整代码或完整命令。

### 跑测期望

- `ctest --test-dir build -R bike_client_tests` → 4 个 TrajectorySim case 全过
- `cmake --build build --target bike_client` → 编译通过(在所有 Task 完成后)
- 手动 smoke(Task 16 清单)→ 在服务端 plan 跑通后进行

---

**End of plan.**
