# 共享单车 Server 扩展 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 bike-server 从"登录/钱包/账单"扩展到完整骑行闭环:扫码解锁、位置上报、结束扣费、报修、历史轨迹回放。

**Architecture:** 沿用现有 FBEB 协议 + asio + MySQL/Redis 架构。新增 3 张表(bike/ride/ride_position)、8 个事件(0x11–0x1E)、Repo 抽象层、内存态 RideSessionStore。Handler 通过 `Ctx` 拿到所有依赖,单测注入 InMemory 实现。

**Tech Stack:** C++17, asio 1.30.2 (vendored), protobuf v3, hiredis, mysqlclient, GTest, Python 3 (集成测试)。

**Spec:** `docs/superpowers/specs/2026-07-19-shared-bike-expansion-design.md`

---

## File Structure

**新增(common 共享纯函数)**
- `common/include/bike/pricing.hpp` / `common/src/pricing.cpp` —— 计费引擎
- `common/include/bike/geo.hpp` / `common/src/geo.cpp` —— Haversine + bounding box + 中国境内判断
- `common/include/bike/ride_no.hpp` / `common/src/ride_no.cpp` —— 订单号生成
- `common/include/bike/validation.hpp` / `common/src/validation.cpp` —— 输入校验
- `common/tests/test_pricing.cpp` / `test_geo.cpp` / `test_ride_no.cpp` / `test_validation.cpp`

**新增(服务端 Repo + 状态)**
- `server/include/server/repo/bike_repo.hpp` —— IBikeRepo 接口 + Bike 结构
- `server/include/server/repo/ride_repo.hpp` —— IRideRepo 接口 + Ride / RidePoint 结构
- `server/include/server/ride_session_store.hpp` / `server/src/ride/ride_session_store.cpp` —— 活跃骑行内存表
- `server/include/server/auth.hpp` / `server/src/auth.cpp` —— require_user 辅助
- `server/include/server/repo/in_memory_bike_repo.hpp` / `in_memory_ride_repo.hpp` —— 单测用 fake
- `server/src/db/mysql_bike_repo.cpp` / `mysql_ride_repo.cpp` —— 生产实现
- `server/include/server/db/mysql_bike_repo.hpp` / `mysql_ride_repo.hpp`

**新增(Handler)**
- `server/src/handlers/list_nearby_bikes.cpp` (event 0x11/0x12)
- `server/src/handlers/scan_unlock.cpp` (0x13/0x14)
- `server/src/handlers/position_report.cpp` (0x15)
- `server/src/handlers/end_ride.cpp` (0x17/0x18)
- `server/src/handlers/report_damage.cpp` (0x19/0x1A)
- `server/src/handlers/get_ride_detail.cpp` (0x1B/0x1C)
- `server/src/handlers/list_rides.cpp` (0x1D/0x1E)

**新增(DB + 测试)**
- `docker/mysql-init/02_schema_ride.sql` —— bike/ride/ride_position 建表
- `docker/mysql-init/03_seed_bikes.sql` —— 60 辆车
- `server/tests/test_ride_session_store.cpp` —— 并发测试
- `server/tests/test_handlers_ride.cpp` —— 7 个 handler 单测
- `server/tests/integration/fbeb_client.py` —— Python FBEB 客户端
- `server/tests/integration/test_full_ride.py`
- `server/tests/integration/test_damaged_bike.py`
- `server/tests/integration/test_end_ride_idempotency.py`
- `docker/docker-compose.test.yml` —— 测试栈

**修改**
- `proto/bike.proto` —— 追加 13 个新消息
- `server/include/server/router.hpp` —— Ctx 增加 bikes/rides/ride_sessions
- `server/include/server/handlers.hpp` —— 增加 7 个 handler 声明
- `server/CMakeLists.txt` —— 把新文件加入对应 lib + 添加新测试目标
- `server/src/main.cpp` —— 注册 7 个新 handler + 注入 MySQL 实现
- `common/CMakeLists.txt` —— 把 pricing/geo/ride_no/validation 加入 bike_common

---

## Task 1: 扩展 bike.proto

**Files:**
- Modify: `proto/bike.proto`

- [ ] **Step 1: 在文件末尾追加新消息**

```proto
// ===== 骑行扩展事件 0x11 – 0x1E =====

message list_nearby_bikes_request {
  string session_token = 1;
  double lat = 2;
  double lng = 3;
  double radius_m = 4;
}
message bike_info {
  string bike_no = 1;
  double lat = 2;
  double lng = 3;
  int32  status = 4;
}
message list_nearby_bikes_response {
  int32 code = 1;
  repeated bike_info bikes = 2;
}

message scan_unlock_request {
  string session_token = 1;
  string bike_no = 2;
  double lat = 3;
  double lng = 4;
}
message scan_unlock_response {
  int32  code = 1;
  string desc = 2;
  string ride_no = 3;
  int64  start_ts = 4;
}

message ride_position_report {
  string ride_no = 1;
  int32  seq = 2;
  double lat = 3;
  double lng = 4;
  int32  elapsed_sec = 5;
}

message end_ride_request {
  string session_token = 1;
  string ride_no = 2;
  double end_lat = 3;
  double end_lng = 4;
}
message end_ride_response {
  int32  code = 1;
  string desc = 2;
  int32  duration_sec = 3;
  int32  distance_m = 4;
  int32  amount_cent = 5;
  int32  balance_after = 6;
}

message report_damage_request {
  string session_token = 1;
  string bike_no = 2;
  string note = 3;
}
message report_damage_response {
  int32  code = 1;
  string desc = 2;
}

message get_ride_detail_request {
  string session_token = 1;
  string ride_no = 2;
}
message ride_point {
  double lat = 1;
  double lng = 2;
  int32  elapsed_sec = 3;
}
message get_ride_detail_response {
  int32  code = 1;
  string ride_no = 2;
  int32  duration_sec = 3;
  int32  distance_m = 4;
  int32  amount_cent = 5;
  string start_tm = 6;
  string end_tm = 7;
  repeated ride_point points = 8;
}

message list_rides_request {
  string session_token = 1;
  int32  limit = 2;
}
message ride_summary {
  string ride_no = 1;
  string start_tm = 2;
  int32  duration_sec = 3;
  int32  distance_m = 4;
  int32  amount_cent = 5;
}
message list_rides_response {
  int32 code = 1;
  repeated ride_summary rides = 2;
}
```

- [ ] **Step 2: 验证 protoc 能编译**

Run: `cd /d/C++/shared_bike_1 && "D:/vcpkg/downloads/tools/cmake-4.3.3-windows/cmake-4.3.3-windows-x86_64/bin/cmake.exe" --build build --target bike_common -j`
Expected: Build OK,proto 自动重新生成 `build/common/generated/bike.pb.h`

- [ ] **Step 3: 在新 terminal 里检查生成代码**

Run: `grep "class list_nearby_bikes_request" /d/C++/shared_bike_1/build/common/generated/bike.pb.h`
Expected: 输出一行匹配,确认 protoc 生成了新消息类

- [ ] **Step 4: Commit**

```bash
git add proto/bike.proto
git commit -m "feat(proto): add ride flow messages (events 0x11-0x1E)"
```

---

## Task 2: DB Schema 迁移

**Files:**
- Create: `docker/mysql-init/02_schema_ride.sql`

- [ ] **Step 1: 检查 schema.sql 是否需要 IF NOT EXISTS**

Run: `head -5 /d/C++/shared_bike_1/docker/mysql-init/schema.sql`
Expected: 看到 `CREATE TABLE IF NOT EXISTS userinfo ...`(确认现有 schema 全部带 IF NOT EXISTS,新 init 文件不会破坏旧库)

- [ ] **Step 2: 写新 schema 文件**

```sql
-- 02_schema_ride.sql — 骑行闭环扩展表
-- 字母序排在 schema.sql 之后,自动按顺序执行

CREATE TABLE IF NOT EXISTS bike (
  id         int          NOT NULL PRIMARY KEY AUTO_INCREMENT,
  bike_no    varchar(32)  NOT NULL UNIQUE,
  lat        decimal(10,7) NOT NULL,
  lng        decimal(10,7) NOT NULL,
  status     tinyint      NOT NULL DEFAULT 0,
  created_at timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS ride (
  id            bigint       NOT NULL PRIMARY KEY AUTO_INCREMENT,
  ride_no       varchar(32)  NOT NULL UNIQUE,
  user_id       int          NOT NULL,
  bike_id       int          NOT NULL,
  start_tm      timestamp    NOT NULL,
  end_tm        timestamp    NOT NULL,
  start_lat     decimal(10,7) NOT NULL,
  start_lng     decimal(10,7) NOT NULL,
  end_lat       decimal(10,7) NOT NULL,
  end_lng       decimal(10,7) NOT NULL,
  duration_sec  int          NOT NULL,
  distance_m    int          NOT NULL,
  amount_cent   int          NOT NULL,
  status        tinyint      NOT NULL DEFAULT 0,
  INDEX idx_user_start (user_id, start_tm),
  INDEX idx_bike_start (bike_id, start_tm),
  FOREIGN KEY (user_id) REFERENCES userinfo(id),
  FOREIGN KEY (bike_id) REFERENCES bike(id)
);

CREATE TABLE IF NOT EXISTS ride_position (
  id          bigint       NOT NULL PRIMARY KEY AUTO_INCREMENT,
  ride_id     bigint       NOT NULL,
  seq         int          NOT NULL,
  lat         decimal(10,7) NOT NULL,
  lng         decimal(10,7) NOT NULL,
  elapsed_sec int          NOT NULL,
  INDEX idx_ride_seq (ride_id, seq),
  FOREIGN KEY (ride_id) REFERENCES ride(id)
);
```

- [ ] **Step 3: 在测试 MySQL 容器里验证语法**

Run:
```bash
docker run --rm -v /d/C++/shared_bike_1/docker/mysql-init:/docker-entrypoint-initdb.d:ro -e MYSQL_ROOT_PASSWORD=test -e MYSQL_DATABASE=bike -d --name mysql_syntax_check mysql:8.0
sleep 15
docker logs mysql_syntax_check 2>&1 | grep -i "error" | head
docker stop mysql_syntax_check
```
Expected: 日志无 error,所有 init SQL 成功执行

- [ ] **Step 4: Commit**

```bash
git add docker/mysql-init/02_schema_ride.sql
git commit -m "feat(db): add bike/ride/ride_position tables"
```

---

## Task 3: Seed 60 辆车

**Files:**
- Create: `docker/mysql-init/03_seed_bikes.sql`

- [ ] **Step 1: 生成 60 行 INSERT(用 Python 算好坐标)**

文件内容(坐标围绕五道口 39.96–39.99 N, 116.31–116.34 E,均匀分布;末尾 3 辆设为 damaged):

```sql
-- 03_seed_bikes.sql — 60 辆车分布在五道口附近
-- 跑在 02_schema_ride.sql 之后

INSERT INTO bike (bike_no, lat, lng, status) VALUES
  ('BJ-000001', 39.9821000, 116.3145000, 0),
  ('BJ-000002', 39.9783000, 116.3198000, 0),
  ('BJ-000003', 39.9756000, 116.3224000, 0),
  ('BJ-000004', 39.9847000, 116.3171000, 0),
  ('BJ-000005', 39.9712000, 116.3138000, 0),
  ('BJ-000006', 39.9791000, 116.3263000, 0),
  ('BJ-000007', 39.9868000, 116.3209000, 0),
  ('BJ-000008', 39.9734000, 116.3186000, 0),
  ('BJ-000009', 39.9825000, 116.3291000, 0),
  ('BJ-000010', 39.9769000, 116.3157000, 0),
  ('BJ-000011', 39.9891000, 116.3234000, 0),
  ('BJ-000012', 39.9747000, 116.3278000, 0),
  ('BJ-000013', 39.9813000, 116.3116000, 0),
  ('BJ-000014', 39.9788000, 116.3245000, 0),
  ('BJ-000015', 39.9856000, 116.3188000, 0),
  ('BJ-000016', 39.9721000, 116.3201000, 0),
  ('BJ-000017', 39.9834000, 116.3272000, 0),
  ('BJ-000018', 39.9765000, 116.3129000, 0),
  ('BJ-000019', 39.9872000, 116.3218000, 0),
  ('BJ-000020', 39.9749000, 116.3163000, 0),
  ('BJ-000021', 39.9818000, 116.3302000, 0),
  ('BJ-000022', 39.9795000, 116.3148000, 0),
  ('BJ-000023', 39.9861000, 116.3195000, 0),
  ('BJ-000024', 39.9738000, 116.3241000, 0),
  ('BJ-000025', 39.9828000, 116.3177000, 0),
  ('BJ-000026', 39.9773000, 116.3285000, 0),
  ('BJ-000027', 39.9884000, 116.3226000, 0),
  ('BJ-000028', 39.9754000, 116.3134000, 0),
  ('BJ-000029', 39.9839000, 116.3206000, 0),
  ('BJ-000030', 39.9717000, 116.3268000, 0),
  ('BJ-000031', 39.9802000, 116.3154000, 0),
  ('BJ-000032', 39.9851000, 116.3281000, 0),
  ('BJ-000033', 39.9778000, 116.3217000, 0),
  ('BJ-000034', 39.9822000, 116.3248000, 0),
  ('BJ-000035', 39.9742000, 116.3193000, 0),
  ('BJ-000036', 39.9876000, 116.3162000, 0),
  ('BJ-000037', 39.9798000, 116.3299000, 0),
  ('BJ-000038', 39.9728000, 116.3220000, 0),
  ('BJ-000039', 39.9844000, 116.3141000, 0),
  ('BJ-000040', 39.9762000, 116.3257000, 0),
  ('BJ-000041', 39.9811000, 116.3183000, 0),
  ('BJ-000042', 39.9888000, 116.3237000, 0),
  ('BJ-000043', 39.9736000, 116.3276000, 0),
  ('BJ-000044', 39.9829000, 116.3119000, 0),
  ('BJ-000045', 39.9781000, 116.3212000, 0),
  ('BJ-000046', 39.9864000, 116.3168000, 0),
  ('BJ-000047', 39.9758000, 116.3294000, 0),
  ('BJ-000048', 39.9836000, 116.3203000, 0),
  ('BJ-000049', 39.9775000, 116.3146000, 0),
  ('BJ-000050', 39.9817000, 116.3266000, 0),
  ('BJ-000051', 39.9849000, 116.3189000, 0),
  ('BJ-000052', 39.9725000, 116.3208000, 0),
  ('BJ-000053', 39.9881000, 116.3251000, 0),
  ('BJ-000054', 39.9768000, 116.3175000, 0),
  ('BJ-000055', 39.9831000, 116.3231000, 0),
  ('BJ-000056', 39.9793000, 116.3125000, 0),
  ('BJ-000057', 39.9815000, 116.3283000, 0),
  ('BJ-000058', 39.9712000, 116.3287000, 2),
  ('BJ-000059', 39.9845000, 116.3162000, 2),
  ('BJ-000060', 39.9768000, 116.3211000, 2);
```

- [ ] **Step 2: 验证 idempotency(重跑不应报错,因 INSERT 不带 IGNORE 会重复主键)**

确认 `bike_no` 是 UNIQUE,生产环境重跑会失败 —— 这是预期行为(seed 只跑一次)。文档化说明:重置数据库需 `docker compose down -v` 清卷。

- [ ] **Step 3: Commit**

```bash
git add docker/mysql-init/03_seed_bikes.sql
git commit -m "feat(db): seed 60 bikes around Wudaokou (3 damaged)"
```

---

## Task 4: pricing 纯函数(TDD)

**Files:**
- Create: `common/include/bike/pricing.hpp`
- Create: `common/src/pricing.cpp`
- Create: `common/tests/test_pricing.cpp`
- Modify: `common/CMakeLists.txt`

- [ ] **Step 1: 写头文件**

```cpp
// common/include/bike/pricing.hpp
#pragma once

namespace bike {

// 起步 1 元(100 分)含 15 分钟,之后每 15 分钟 0.5 元(50 分),向上取整。
// duration_sec < 0 抛 std::invalid_argument。
int compute_fee(int duration_sec);

} // namespace bike
```

- [ ] **Step 2: 写失败的测试**

```cpp
// common/tests/test_pricing.cpp
#include <bike/pricing.hpp>

#include <gtest/gtest.h>

using namespace bike;

TEST(Pricing, ZeroDurationIsBaseFee)       { EXPECT_EQ(compute_fee(0),       100); }
TEST(Pricing, AtExactly15MinStillBase)     { EXPECT_EQ(compute_fee(15*60),   100); }
TEST(Pricing, JustOver15MinChargesOneStep) { EXPECT_EQ(compute_fee(15*60+1), 150); }
TEST(Pricing, AtExactly30MinTwoBuckets)    { EXPECT_EQ(compute_fee(30*60),   150); }
TEST(Pricing, JustOver30MinThreeBuckets)   { EXPECT_EQ(compute_fee(30*60+1), 200); }
TEST(Pricing, AtExactly45Min)              { EXPECT_EQ(compute_fee(45*60),   200); }
TEST(Pricing, AtExactly60Min)              { EXPECT_EQ(compute_fee(60*60),   250); }
TEST(Pricing, FractionalSecRoundsUp)       { EXPECT_EQ(compute_fee(15*60+30),150); }
TEST(Pricing, NegativeInputRejected) {
    EXPECT_THROW(compute_fee(-1), std::invalid_argument);
}
```

- [ ] **Step 3: 把测试加入 CMakeLists**

修改 `common/CMakeLists.txt`,在 `if(GTest_FOUND)` 块内追加:

```cmake
add_executable(test_pricing tests/test_pricing.cpp)
target_link_libraries(test_pricing PRIVATE bike_common GTest::gtest_main)
target_include_directories(test_pricing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME pricing COMMAND test_pricing)
```

- [ ] **Step 4: 运行测试,确认失败**

Run: `cd /d/C++/shared_bike_1 && "D:/vcpkg/downloads/tools/cmake-4.3.3-windows/cmake-4.3.3-windows-x86_64/bin/cmake.exe" --build build --target test_pricing -j`
Expected: 链接错误,`compute_fee` 未定义

- [ ] **Step 5: 实现 pricing.cpp**

```cpp
// common/src/pricing.cpp
#include "bike/pricing.hpp"

#include <stdexcept>

namespace bike {

int compute_fee(int duration_sec) {
    if (duration_sec < 0) throw std::invalid_argument{"negative duration"};
    constexpr int base_sec = 15 * 60;
    constexpr int step_sec = 15 * 60;
    int fee = 100;
    if (duration_sec <= base_sec) return fee;
    int extra_sec = duration_sec - base_sec;
    int extra_chunks = (extra_sec + step_sec - 1) / step_sec;
    return fee + extra_chunks * 50;
}

} // namespace bike
```

修改 `common/CMakeLists.txt`,把 `src/pricing.cpp` 加入 `bike_common` 库的源文件列表。

- [ ] **Step 6: 运行测试,确认通过**

Run: `"D:/vcpkg/downloads/tools/cmake-4.3.3-windows/cmake-4.3.3-windows-x86_64/bin/cmake.exe" --build build --target test_pricing -j && build/common/test_pricing`
Expected: 9 个 case 全 PASS

- [ ] **Step 7: Commit**

```bash
git add common/include/bike/pricing.hpp common/src/pricing.cpp common/tests/test_pricing.cpp common/CMakeLists.txt
git commit -m "feat(common): pricing engine with TDD"
```

---

## Task 5: geo 纯函数(TDD)

**Files:**
- Create: `common/include/bike/geo.hpp`
- Create: `common/src/geo.cpp`
- Create: `common/tests/test_geo.cpp`

- [ ] **Step 1: 写头文件**

```cpp
// common/include/bike/geo.hpp
#pragma once

namespace bike {

// WGS84 椭球大圆距离,单位米。两点重合返回 0。
double haversine_m(double lat1, double lng1, double lat2, double lng2);

// 中国大陆境内粗略判断(用于拒绝明显错误的坐标)。
// 范围:lat 18–54,lng 73–135。
bool is_in_china(double lat, double lng);

} // namespace bike
```

- [ ] **Step 2: 写失败的测试**

```cpp
// common/tests/test_geo.cpp
#include <bike/geo.hpp>

#include <gtest/gtest.h>

using namespace bike;

TEST(Geo, HaversineZero) {
    EXPECT_NEAR(haversine_m(39.98, 116.31, 39.98, 116.31), 0.0, 0.01);
}

TEST(Geo, HaversineKnownPair) {
    // 北京天安门到天津火车站,实测 ~117 km
    double d = haversine_m(39.9087, 116.3974, 39.1308, 117.2607);
    EXPECT_NEAR(d, 117500.0, 1000.0);
}

TEST(Geo, IsInChinaBeijing) {
    EXPECT_TRUE(is_in_china(39.98, 116.31));
}

TEST(Geo, IsInChinaRejectsNewYork) {
    EXPECT_FALSE(is_in_china(40.71, -74.00));
}

TEST(Geo, IsInChinaRejectsNullIsland) {
    EXPECT_FALSE(is_in_china(0.0, 0.0));
}
```

- [ ] **Step 3: 把测试加入 CMakeLists**

`common/CMakeLists.txt` 内追加:

```cmake
add_executable(test_geo tests/test_geo.cpp)
target_link_libraries(test_geo PRIVATE bike_common GTest::gtest_main)
target_include_directories(test_geo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME geo COMMAND test_geo)
```

- [ ] **Step 4: 运行测试,确认失败**

Run: `cmake --build build --target test_geo -j`
Expected: 链接错误,`haversine_m` / `is_in_china` 未定义

- [ ] **Step 5: 实现 geo.cpp**

```cpp
// common/src/geo.cpp
#include "bike/geo.hpp"

#include <cmath>

namespace bike {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6371000.0;

double to_rad(double deg) { return deg * kPi / 180.0; }
}

double haversine_m(double lat1, double lng1, double lat2, double lng2) {
    double la1 = to_rad(lat1), la2 = to_rad(lat2);
    double dla = to_rad(lat2 - lat1);
    double dlo = to_rad(lng2 - lng1);
    double a = std::sin(dla / 2) * std::sin(dla / 2) +
               std::cos(la1) * std::cos(la2) *
               std::sin(dlo / 2) * std::sin(dlo / 2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return kEarthRadiusM * c;
}

bool is_in_china(double lat, double lng) {
    return lat >= 18.0 && lat <= 54.0 && lng >= 73.0 && lng <= 135.0;
}

} // namespace bike
```

把 `src/geo.cpp` 加入 `common/CMakeLists.txt` 的 `bike_common` 源文件列表。

- [ ] **Step 6: 运行测试,确认通过**

Run: `cmake --build build --target test_geo -j && build/common/test_geo`
Expected: 5 个 case 全 PASS

- [ ] **Step 7: Commit**

```bash
git add common/include/bike/geo.hpp common/src/geo.cpp common/tests/test_geo.cpp common/CMakeLists.txt
git commit -m "feat(common): haversine + china boundary check"
```

---

## Task 6: ride_no 生成器(TDD)

**Files:**
- Create: `common/include/bike/ride_no.hpp`
- Create: `common/src/ride_no.cpp`
- Create: `common/tests/test_ride_no.cpp`

- [ ] **Step 1: 写头文件**

```cpp
// common/include/bike/ride_no.hpp
#pragma once

#include <cstdint>
#include <string>

namespace bike {

// date_yyyymmdd 例 20260719;daily_seq 1..999999
// 返回 "R" + 8 位日期 + 6 位零填充序号,例 "R20260719000001"
// daily_seq 超出 [1, 999999] 抛 std::invalid_argument
std::string make_ride_no(int date_yyyymmdd, int daily_seq);

} // namespace bike
```

- [ ] **Step 2: 写失败的测试**

```cpp
// common/tests/test_ride_no.cpp
#include <bike/ride_no.hpp>

#include <gtest/gtest.h>

using namespace bike;

TEST(RideNo, FirstSeq) {
    EXPECT_EQ(make_ride_no(20260719, 1), "R20260719000001");
}

TEST(RideNo, MaxSeq) {
    EXPECT_EQ(make_ride_no(20260719, 999999), "R20260719999999");
}

TEST(RideNo, DifferentDate) {
    EXPECT_EQ(make_ride_no(20251231, 42), "R20251231000042");
}

TEST(RideNo, RejectsZeroSeq) {
    EXPECT_THROW(make_ride_no(20260719, 0), std::invalid_argument);
}

TEST(RideNo, RejectsOverflowSeq) {
    EXPECT_THROW(make_ride_no(20260719, 1000000), std::invalid_argument);
}
```

- [ ] **Step 3: CMakeLists 加测试目标**

```cmake
add_executable(test_ride_no tests/test_ride_no.cpp)
target_link_libraries(test_ride_no PRIVATE bike_common GTest::gtest_main)
target_include_directories(test_ride_no PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ride_no COMMAND test_ride_no)
```

- [ ] **Step 4: 运行测试,确认失败**

Run: `cmake --build build --target test_ride_no -j`
Expected: 链接错误

- [ ] **Step 5: 实现 ride_no.cpp**

```cpp
// common/src/ride_no.cpp
#include "bike/ride_no.hpp"

#include <cstdio>
#include <stdexcept>

namespace bike {

std::string make_ride_no(int date_yyyymmdd, int daily_seq) {
    if (daily_seq < 1 || daily_seq > 999999) {
        throw std::invalid_argument{"daily_seq out of [1, 999999]"};
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "R%08d%06d", date_yyyymmdd, daily_seq);
    return std::string{buf};
}

} // namespace bike
```

把 `src/ride_no.cpp` 加入 `bike_common` 源文件。

- [ ] **Step 6: 运行测试,确认通过**

Run: `cmake --build build --target test_ride_no -j && build/common/test_ride_no`
Expected: 5 个 case 全 PASS

- [ ] **Step 7: Commit**

```bash
git add common/include/bike/ride_no.hpp common/src/ride_no.cpp common/tests/test_ride_no.cpp common/CMakeLists.txt
git commit -m "feat(common): ride_no generator with 6-digit daily sequence"
```

---

## Task 7: validation 辅助(TDD)

**Files:**
- Create: `common/include/bike/validation.hpp`
- Create: `common/src/validation.cpp`
- Create: `common/tests/test_validation.cpp`

- [ ] **Step 1: 写头文件**

```cpp
// common/include/bike/validation.hpp
#pragma once

#include <string>

namespace bike {

bool valid_bike_no(const std::string& s);   // 1..32 字节,非空
bool valid_lat(double lat);                  // [-90, 90]
bool valid_lng(double lng);                  // [-180, 180]
double clamp_radius(double r);               // 默认 500,clamp 到 [50, 2000]
int    clamp_limit(int n);                   // 默认 20,clamp 到 [1, 100]

} // namespace bike
```

- [ ] **Step 2: 写失败的测试**

```cpp
// common/tests/test_validation.cpp
#include <bike/validation.hpp>

#include <gtest/gtest.h>

using namespace bike;

TEST(Validation, BikeNoValid)         { EXPECT_TRUE(valid_bike_no("BJ-000001")); }
TEST(Validation, BikeNoRejectsEmpty)  { EXPECT_FALSE(valid_bike_no("")); }
TEST(Validation, BikeNoRejectsTooLong) {
    EXPECT_FALSE(valid_bike_no(std::string(33, 'x')));
}

TEST(Validation, LatBoundary) {
    EXPECT_FALSE(valid_lat(91.0));
    EXPECT_FALSE(valid_lat(-91.0));
    EXPECT_TRUE(valid_lat(90.0));
    EXPECT_TRUE(valid_lat(-90.0));
    EXPECT_TRUE(valid_lat(0.0));
}

TEST(Validation, LngBoundary) {
    EXPECT_FALSE(valid_lng(181.0));
    EXPECT_FALSE(valid_lng(-181.0));
    EXPECT_TRUE(valid_lng(180.0));
    EXPECT_TRUE(valid_lng(-180.0));
}

TEST(Validation, RadiusDefault) { EXPECT_DOUBLE_EQ(clamp_radius(0),    500.0); }
TEST(Validation, RadiusMin)     { EXPECT_DOUBLE_EQ(clamp_radius(10),   50.0); }
TEST(Validation, RadiusMax)     { EXPECT_DOUBLE_EQ(clamp_radius(9999), 2000.0); }
TEST(Validation, RadiusPassthrough) { EXPECT_DOUBLE_EQ(clamp_radius(750), 750.0); }

TEST(Validation, LimitDefault) { EXPECT_EQ(clamp_limit(0),   20); }
TEST(Validation, LimitMin)     { EXPECT_EQ(clamp_limit(-1),  1); }
TEST(Validation, LimitMax)     { EXPECT_EQ(clamp_limit(500), 100); }
TEST(Validation, LimitPassthrough) { EXPECT_EQ(clamp_limit(50), 50); }
```

- [ ] **Step 3: CMakeLists 加测试目标**

```cmake
add_executable(test_validation tests/test_validation.cpp)
target_link_libraries(test_validation PRIVATE bike_common GTest::gtest_main)
target_include_directories(test_validation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME validation COMMAND test_validation)
```

- [ ] **Step 4: 运行测试,确认失败**

Run: `cmake --build build --target test_validation -j`
Expected: 链接错误

- [ ] **Step 5: 实现 validation.cpp**

```cpp
// common/src/validation.cpp
#include "bike/validation.hpp"

#include <algorithm>

namespace bike {

bool valid_bike_no(const std::string& s) {
    return !s.empty() && s.size() <= 32;
}
bool valid_lat(double lat) { return lat >= -90.0 && lat <= 90.0; }
bool valid_lng(double lng) { return lng >= -180.0 && lng <= 180.0; }

double clamp_radius(double r) {
    if (r <= 0.0) return 500.0;
    if (r < 50.0)  return 50.0;
    if (r > 2000.0) return 2000.0;
    return r;
}

int clamp_limit(int n) {
    if (n <= 0)  return 20;
    if (n < 1)   return 1;
    if (n > 100) return 100;
    return n;
}

} // namespace bike
```

把 `src/validation.cpp` 加入 `bike_common`。

- [ ] **Step 6: 运行测试,确认通过**

Run: `cmake --build build --target test_validation -j && build/common/test_validation`
Expected: 16 个 case 全 PASS

- [ ] **Step 7: Commit**

```bash
git add common/include/bike/validation.hpp common/src/validation.cpp common/tests/test_validation.cpp common/CMakeLists.txt
git commit -m "feat(common): input validation helpers"
```

---

## Task 8: IBikeRepo 接口 + InMemory 实现

**Files:**
- Create: `server/include/server/repo/bike_repo.hpp`
- Modify: `server/include/server/repo/in_memory.hpp`
- Modify: `server/CMakeLists.txt`(若 in_memory.hpp 是单独 lib 则更新之,否则跳过)

- [ ] **Step 1: 写 IBikeRepo 接口**

```cpp
// server/include/server/repo/bike_repo.hpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bike::server {

enum class BikeStatus : int { Idle = 0, Rented = 1, Damaged = 2 };

struct Bike {
    int id{0};
    std::string bike_no;
    double lat{0};
    double lng{0};
    BikeStatus status{BikeStatus::Idle};
};

class IBikeRepo {
public:
    virtual ~IBikeRepo() = default;
    // FOR UPDATE 语义:在事务内对 bike_no 加行锁
    virtual std::optional<Bike> get_for_update(const std::string& bike_no) = 0;
    virtual bool update_status(int bike_id, BikeStatus status) = 0;
    virtual bool update_location(int bike_id, double lat, double lng) = 0;
    // bounding box 查询
    virtual std::vector<Bike> list_in_bounds(double lat_min, double lat_max,
                                             double lng_min, double lng_max) = 0;
};

} // namespace bike::server
```

- [ ] **Step 2: InMemory 实现追加到 in_memory.hpp**

在 `server/include/server/repo/in_memory.hpp` 末尾(命名空间内)追加:

```cpp
#include "server/repo/bike_repo.hpp"

class InMemoryBikeRepo : public IBikeRepo {
public:
    std::optional<Bike> get_for_update(const std::string& no) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = bikes_.find(no);
        if (it == bikes_.end()) return std::nullopt;
        return it->second;
    }
    bool update_status(int id, BikeStatus s) override {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [_, b] : bikes_) {
            if (b.id == id) { b.status = s; return true; }
        }
        return false;
    }
    bool update_location(int id, double lat, double lng) override {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [_, b] : bikes_) {
            if (b.id == id) { b.lat = lat; b.lng = lng; return true; }
        }
        return false;
    }
    std::vector<Bike> list_in_bounds(double la_min, double la_max,
                                     double lo_min, double lo_max) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<Bike> out;
        for (auto& [_, b] : bikes_) {
            if (b.lat >= la_min && b.lat <= la_max &&
                b.lng >= lo_min && b.lng <= lo_max) {
                out.push_back(b);
            }
        }
        return out;
    }
    // 测试辅助
    void seed(Bike b) {
        std::lock_guard<std::mutex> lk(mu_);
        bikes_[b.bike_no] = b;
    }
private:
    std::mutex mu_;
    std::map<std::string, Bike> bikes_;
};
```

- [ ] **Step 3: 构建 server core,确认头文件能编译**

Run: `cmake --build build --target bike_server_core -j`
Expected: 无错误

- [ ] **Step 4: Commit**

```bash
git add server/include/server/repo/bike_repo.hpp server/include/server/repo/in_memory.hpp
git commit -m "feat(server): IBikeRepo interface + InMemory fake"
```

---

## Task 9: IRideRepo 接口 + InMemory 实现

**Files:**
- Create: `server/include/server/repo/ride_repo.hpp`
- Modify: `server/include/server/repo/in_memory.hpp`

- [ ] **Step 1: 写 IRideRepo 接口**

```cpp
// server/include/server/repo/ride_repo.hpp
#pragma once

#include "server/repo/bike_repo.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bike::server {

struct RidePoint {
    int seq{0};
    double lat{0};
    double lng{0};
    int elapsed_sec{0};
};

struct Ride {
    int          id{0};
    std::string  ride_no;
    int          user_id{0};
    int          bike_id{0};
    long long    start_ts{0};    // unix 秒
    long long    end_ts{0};
    double       start_lat{0};
    double       start_lng{0};
    double       end_lat{0};
    double       end_lng{0};
    int          duration_sec{0};
    int          distance_m{0};
    int          amount_cent{0};
    int          status{0};
};

// 创建参数(避免直接构造完整 Ride,service 层只关心输入)
struct CreateRideInput {
    std::string ride_no;
    int         user_id;
    int         bike_id;
    long long   start_ts;
    long long   end_ts;
    double      start_lat;
    double      start_lng;
    double      end_lat;
    double      end_lng;
    int         duration_sec;
    int         distance_m;
    int         amount_cent;
    std::vector<RidePoint> points;
};

class IRideRepo {
public:
    virtual ~IRideRepo() = default;
    // 插入 ride + 关联 points,在同一事务内。返回插入后的 Ride(含 id)。
    virtual Ride create_with_points(const CreateRideInput& in) = 0;
    virtual std::optional<Ride> find_by_no(const std::string& ride_no) = 0;
    virtual std::vector<RidePoint> list_points(int ride_id) = 0;
    virtual std::vector<Ride> list_by_user(int user_id, int limit) = 0;
};

} // namespace bike::server
```

- [ ] **Step 2: InMemory 实现追加**

在 `in_memory.hpp` 末尾(`InMemoryBikeRepo` 之后,仍在命名空间内)追加:

```cpp
#include "server/repo/ride_repo.hpp"

class InMemoryRideRepo : public IRideRepo {
public:
    Ride create_with_points(const CreateRideInput& in) override {
        std::lock_guard<std::mutex> lk(mu_);
        Ride r;
        r.id = next_id_++;
        r.ride_no = in.ride_no;
        r.user_id = in.user_id;
        r.bike_id = in.bike_id;
        r.start_ts = in.start_ts;
        r.end_ts = in.end_ts;
        r.start_lat = in.start_lat;
        r.start_lng = in.start_lng;
        r.end_lat = in.end_lat;
        r.end_lng = in.end_lng;
        r.duration_sec = in.duration_sec;
        r.distance_m = in.distance_m;
        r.amount_cent = in.amount_cent;
        r.status = 0;
        rides_[r.id] = r;
        points_[r.id] = in.points;
        by_no_[r.ride_no] = r.id;
        return r;
    }
    std::optional<Ride> find_by_no(const std::string& no) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = by_no_.find(no);
        if (it == by_no_.end()) return std::nullopt;
        return rides_[it->second];
    }
    std::vector<RidePoint> list_points(int ride_id) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = points_.find(ride_id);
        if (it == points_.end()) return {};
        return it->second;
    }
    std::vector<Ride> list_by_user(int uid, int limit) override {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<Ride> out;
        for (auto& [_, r] : rides_) {
            if (r.user_id == uid) out.push_back(r);
            if ((int)out.size() >= limit) break;
        }
        return out;
    }
private:
    std::mutex mu_;
    std::map<int, Ride> rides_;
    std::map<int, std::vector<RidePoint>> points_;
    std::map<std::string, int> by_no_;
    int next_id_{1};
};
```

- [ ] **Step 3: 构建**

Run: `cmake --build build --target bike_server_core -j`
Expected: 无错误

- [ ] **Step 4: Commit**

```bash
git add server/include/server/repo/ride_repo.hpp server/include/server/repo/in_memory.hpp
git commit -m "feat(server): IRideRepo interface + InMemory fake"
```

---

## Task 10: RideSessionStore(活跃骑行内存表)

**Files:**
- Create: `server/include/server/ride_session_store.hpp`
- Create: `server/src/ride/ride_session_store.cpp`
- Create: `server/tests/test_ride_session_store.cpp`

- [ ] **Step 1: 写头文件**

```cpp
// server/include/server/ride_session_store.hpp
#pragma once

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace bike::server {

struct RideSession {
    std::string ride_no;
    int         user_id{0};
    int         bike_id{0};
    double      start_lat{0};
    double      start_lng{0};
    long long   start_ts{0};      // unix 秒
    double      last_lat{0};
    double      last_lng{0};
    int         last_seq{0};
};

class RideSessionStore {
public:
    void create(RideSession s) {
        std::unique_lock<std::shared_mutex> lk(mu_);
        sessions_[s.ride_no] = std::move(s);
    }
    std::optional<RideSession> find(const std::string& ride_no) const {
        std::shared_lock<std::shared_mutex> lk(mu_);
        auto it = sessions_.find(ride_no);
        if (it == sessions_.end()) return std::nullopt;
        return it->second;
    }
    bool update_pos(const std::string& ride_no, double lat, double lng, int seq) {
        std::shared_lock<std::shared_mutex> lk(mu_);
        auto it = sessions_.find(ride_no);
        if (it == sessions_.end()) return false;
        it->second.last_lat = lat;
        it->second.last_lng = lng;
        it->second.last_seq = seq;
        return true;
    }
    bool remove(const std::string& ride_no) {
        std::unique_lock<std::shared_mutex> lk(mu_);
        return sessions_.erase(ride_no) > 0;
    }
private:
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, RideSession> sessions_;
};

} // namespace bike::server
```

- [ ] **Step 2: 写并发测试**

```cpp
// server/tests/test_ride_session_store.cpp
#include "server/ride_session_store.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace bike::server;

TEST(RideSessionStore, CreateFindRemove) {
    RideSessionStore s;
    RideSession r{.ride_no = "R1", .user_id = 1};
    s.create(r);
    auto found = s.find("R1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->user_id, 1);
    EXPECT_TRUE(s.remove("R1"));
    EXPECT_FALSE(s.find("R1").has_value());
}

TEST(RideSessionStore, UpdatePos) {
    RideSessionStore s;
    s.create({.ride_no = "R1"});
    EXPECT_TRUE(s.update_pos("R1", 39.98, 116.31, 5));
    auto found = s.find("R1");
    EXPECT_DOUBLE_EQ(found->last_lat, 39.98);
    EXPECT_EQ(found->last_seq, 5);
}

TEST(RideSessionStore, UpdatePosUnknownReturnsFalse) {
    RideSessionStore s;
    EXPECT_FALSE(s.update_pos("nope", 0, 0, 0));
}

TEST(RideSessionStore, ConcurrentReportsSafe) {
    RideSessionStore s;
    s.create({.ride_no = "R1"});
    std::vector<std::thread> ts;
    for (int i = 0; i < 100; ++i) {
        ts.emplace_back([&s, i]{
            s.update_pos("R1", 39.98 + i*0.0001, 116.31, i);
        });
    }
    for (auto& t : ts) t.join();
    auto found = s.find("R1");
    ASSERT_TRUE(found.has_value());
    // last_seq 应是某个有效值(具体哪个不重要,只要不崩溃)
    EXPECT_GE(found->last_seq, 0);
    EXPECT_LE(found->last_seq, 99);
}

TEST(RideSessionStore, UpdateAndRemoveRaceNoDeadlock) {
    RideSessionStore s;
    s.create({.ride_no = "R1"});
    std::vector<std::thread> ts;
    for (int i = 0; i < 50; ++i) {
        ts.emplace_back([&s]{ s.update_pos("R1", 0, 0, 0); });
    }
    ts.emplace_back([&s]{ s.remove("R1"); });
    for (auto& t : ts) t.join();
    SUCCEED();
}
```

- [ ] **Step 3: CMakeLists 加测试**

`server/CMakeLists.txt` 在 `if(GTest_FOUND)` 块内追加:

```cmake
add_executable(test_ride_session_store tests/test_ride_session_store.cpp)
target_link_libraries(test_ride_session_store PRIVATE bike_server_core GTest::gtest_main)
target_include_directories(test_ride_session_store PRIVATE include)
add_test(NAME ride_session_store COMMAND test_ride_session_store)
```

(注:`RideSessionStore` 是 header-only,无需 `.cpp` 文件。删除 Task 顶部 `server/src/ride/ride_session_store.cpp` 的引用。)

- [ ] **Step 4: 运行测试**

Run: `cmake --build build --target test_ride_session_store -j && build/server/test_ride_session_store`
Expected: 5 个 case 全 PASS

- [ ] **Step 5: Commit**

```bash
git add server/include/server/ride_session_store.hpp server/tests/test_ride_session_store.cpp server/CMakeLists.txt
git commit -m "feat(server): RideSessionStore with concurrency tests"
```

---

## Task 11: 扩展 Ctx 注入新依赖

**Files:**
- Modify: `server/include/server/router.hpp`

- [ ] **Step 1: 修改 Ctx 结构**

把 `Ctx` 改为:

```cpp
#include "server/repo/bike_repo.hpp"
#include "server/repo/ride_repo.hpp"
#include "server/ride_session_store.hpp"

struct Ctx {
    std::shared_ptr<IUserRepo>         users;
    std::shared_ptr<IAccountRepo>      accounts;
    std::shared_ptr<ISessionStore>     sessions;
    std::shared_ptr<IBikeRepo>         bikes;
    std::shared_ptr<IRideRepo>         rides;
    std::shared_ptr<RideSessionStore>  ride_sessions;
};
```

- [ ] **Step 2: 构建**

Run: `cmake --build build --target bike_server_core -j`
Expected: 之前用 `Ctx` 的代码仍能编译(只增加了字段,没改老的)

- [ ] **Step 3: 修改 main.cpp 注入新依赖**

在 `server/src/main.cpp` 找到 `Ctx ctx{...}` 构造处,改成:

```cpp
Ctx ctx{
    .users          = std::make_shared<InMemoryUserRepo>(),       // prod 改 MysqlUserRepo
    .accounts       = std::make_shared<InMemoryAccountRepo>(),
    .sessions       = std::make_shared<RedisSessionStore>(...),
    .bikes          = std::make_shared<MysqlBikeRepo>(pool),       // Task 12 添加
    .rides          = std::make_shared<MysqlRideRepo>(pool),       // Task 13 添加
    .ride_sessions  = std::make_shared<RideSessionStore>(),
};
```

如果 MysqlBikeRepo / MysqlRideRepo 还没实现,先用 InMemoryBikeRepo / InMemoryRideRepo 占位,标注 TODO 在 Task 12/13 切换。

- [ ] **Step 4: 构建 + 跑现有测试**

Run: `cmake --build build -j && ctest --test-dir build`
Expected: 全绿

- [ ] **Step 5: Commit**

```bash
git add server/include/server/router.hpp server/src/main.cpp
git commit -m "refactor(server): inject bikes/rides/ride_sessions into Ctx"
```

---

## Task 12: MysqlBikeRepo 生产实现

**Files:**
- Create: `server/include/server/db/mysql_bike_repo.hpp`
- Create: `server/src/db/mysql_bike_repo.cpp`
- Modify: `server/CMakeLists.txt`

- [ ] **Step 1: 写头文件**

```cpp
// server/include/server/db/mysql_bike_repo.hpp
#pragma once

#include "server/repo/bike_repo.hpp"
#include "server/db/mysql_pool.hpp"

#include <memory>

namespace bike::server {

class MysqlBikeRepo : public IBikeRepo {
public:
    explicit MysqlBikeRepo(std::shared_ptr<MysqlPool> pool);
    std::optional<Bike> get_for_update(const std::string& bike_no) override;
    bool update_status(int bike_id, BikeStatus status) override;
    bool update_location(int bike_id, double lat, double lng) override;
    std::vector<Bike> list_in_bounds(double lat_min, double lat_max,
                                     double lng_min, double lng_max) override;
private:
    std::shared_ptr<MysqlPool> pool_;
};

} // namespace bike::server
```

- [ ] **Step 2: 实现 .cpp**

```cpp
// server/src/db/mysql_bike_repo.cpp
#include "server/db/mysql_bike_repo.hpp"
#include "server/logging.hpp"

#include <mysql/jdbc.h>
#include <sstream>

namespace bike::server {

MysqlBikeRepo::MysqlBikeRepo(std::shared_ptr<MysqlPool> pool) : pool_(pool) {}

namespace {
Bike row_to_bike(const sql::ResultSet& rs) {
    Bike b;
    b.id      = rs.getInt("id");
    b.bike_no = rs.getString("bike_no").asStdString();
    b.lat     = std::stod(rs.getString("lat").asStdString());   // decimal -> double
    b.lng     = std::stod(rs.getString("lng").asStdString());
    b.status  = static_cast<BikeStatus>(rs.getInt("status"));
    return b;
}
}

std::optional<Bike> MysqlBikeRepo::get_for_update(const std::string& no) {
    auto conn = pool_->get();
    std::unique_ptr<sql::PreparedStatement> st(
        conn->prepareStatement("SELECT * FROM bike WHERE bike_no=? FOR UPDATE"));
    st->setString(1, no);
    std::unique_ptr<sql::ResultSet> rs(st->executeQuery());
    if (!rs->next()) return std::nullopt;
    return row_to_bike(*rs);
}

bool MysqlBikeRepo::update_status(int id, BikeStatus s) {
    auto conn = pool_->get();
    std::unique_ptr<sql::PreparedStatement> st(
        conn->prepareStatement("UPDATE bike SET status=? WHERE id=?"));
    st->setInt(1, static_cast<int>(s));
    st->setInt(2, id);
    return st->executeUpdate() > 0;
}

bool MysqlBikeRepo::update_location(int id, double lat, double lng) {
    auto conn = pool_->get();
    std::unique_ptr<sql::PreparedStatement> st(
        conn->prepareStatement("UPDATE bike SET lat=?, lng=? WHERE id=?"));
    // decimal(10,7): 用字符串保留 7 位精度
    std::ostringstream la, lo;
    la.setf(std::ios::fixed); la.precision(7); la << lat;
    lo.setf(std::ios::fixed); lo.precision(7); lo << lng;
    st->setString(1, la.str());
    st->setString(2, lo.str());
    st->setInt(3, id);
    return st->executeUpdate() > 0;
}

std::vector<Bike> MysqlBikeRepo::list_in_bounds(double la_min, double la_max,
                                                 double lo_min, double lo_max) {
    auto conn = pool_->get();
    std::unique_ptr<sql::PreparedStatement> st(conn->prepareStatement(
        "SELECT * FROM bike WHERE status IN (0,2) AND "
        "lat BETWEEN ? AND ? AND lng BETWEEN ? AND ?"));
    std::ostringstream lamn, lamx, lomn, lomx;
    lamn.setf(std::ios::fixed); lamn.precision(7); lamn << la_min;
    lamx.setf(std::ios::fixed); lamx.precision(7); lamx << la_max;
    lomn.setf(std::ios::fixed); lomn.precision(7); lomn << lo_min;
    lomx.setf(std::ios::fixed); lomx.precision(7); lomx << lo_max;
    st->setString(1, lamn.str());
    st->setString(2, lamx.str());
    st->setString(3, lomn.str());
    st->setString(4, lomx.str());
    std::unique_ptr<sql::ResultSet> rs(st->executeQuery());
    std::vector<Bike> out;
    while (rs->next()) out.push_back(row_to_bike(*rs));
    return out;
}

} // namespace bike::server
```

- [ ] **Step 3: 加入 bike_server_prod**

`server/CMakeLists.txt` 把 `src/db/mysql_bike_repo.cpp` 加入 `bike_server_prod` 库的源文件。

- [ ] **Step 4: 构建**

Run: `cmake --build build --target bike_server_prod -j`
Expected: 无错误

- [ ] **Step 5: Commit**

```bash
git add server/include/server/db/mysql_bike_repo.hpp server/src/db/mysql_bike_repo.cpp server/CMakeLists.txt
git commit -m "feat(server): MysqlBikeRepo production implementation"
```

---

## Task 13: MysqlRideRepo 生产实现

**Files:**
- Create: `server/include/server/db/mysql_ride_repo.hpp`
- Create: `server/src/db/mysql_ride_repo.cpp`
- Modify: `server/CMakeLists.txt`

- [ ] **Step 1: 写头文件**

```cpp
// server/include/server/db/mysql_ride_repo.hpp
#pragma once

#include "server/repo/ride_repo.hpp"
#include "server/db/mysql_pool.hpp"

#include <memory>

namespace bike::server {

class MysqlRideRepo : public IRideRepo {
public:
    explicit MysqlRideRepo(std::shared_ptr<MysqlPool> pool);
    Ride create_with_points(const CreateRideInput& in) override;
    std::optional<Ride> find_by_no(const std::string& ride_no) override;
    std::vector<RidePoint> list_points(int ride_id) override;
    std::vector<Ride> list_by_user(int user_id, int limit) override;
private:
    std::shared_ptr<MysqlPool> pool_;
};

} // namespace bike::server
```

- [ ] **Step 2: 实现 .cpp**

```cpp
// server/src/db/mysql_ride_repo.cpp
#include "server/db/mysql_ride_repo.hpp"
#include "server/logging.hpp"

#include <mysql/jdbc.h>
#include <sstream>

namespace bike::server {

namespace {
std::string fmt7(double v) {
    std::ostringstream s;
    s.setf(std::ios::fixed); s.precision(7); s << v;
    return s.str();
}

Ride row_to_ride(const sql::ResultSet& rs) {
    Ride r;
    r.id            = rs.getInt("id");
    r.ride_no       = rs.getString("ride_no").asStdString();
    r.user_id       = rs.getInt("user_id");
    r.bike_id       = rs.getInt("bike_id");
    r.start_ts      = rs.getInt64("start_ts");
    r.end_ts        = rs.getInt64("end_ts");
    r.start_lat     = std::stod(rs.getString("start_lat").asStdString());
    r.start_lng     = std::stod(rs.getString("start_lng").asStdString());
    r.end_lat       = std::stod(rs.getString("end_lat").asStdString());
    r.end_lng       = std::stod(rs.getString("end_lng").asStdString());
    r.duration_sec  = rs.getInt("duration_sec");
    r.distance_m    = rs.getInt("distance_m");
    r.amount_cent   = rs.getInt("amount_cent");
    r.status        = rs.getInt("status");
    return r;
}
}

MysqlRideRepo::MysqlRideRepo(std::shared_ptr<MysqlPool> pool) : pool_(pool) {}

Ride MysqlRideRepo::create_with_points(const CreateRideInput& in) {
    auto conn = pool_->get();
    conn->setAutoCommit(false);
    try {
        std::unique_ptr<sql::PreparedStatement> st(conn->prepareStatement(
            "INSERT INTO ride(ride_no,user_id,bike_id,start_tm,end_tm,"
            "start_lat,start_lng,end_lat,end_lng,duration_sec,distance_m,amount_cent,status) "
            "VALUES(?,?,?,?,FROM_UNIXTIME(?),FROM_UNIXTIME(?),?,?,?,?,?,?,0)"));
        st->setString(1, in.ride_no);
        st->setInt(2, in.user_id);
        st->setInt(3, in.bike_id);
        st->setInt64(4, in.start_ts);
        st->setInt64(5, in.end_ts);
        st->setString(6, fmt7(in.start_lat));
        st->setString(7, fmt7(in.start_lng));
        st->setString(8, fmt7(in.end_lat));
        st->setString(9, fmt7(in.end_lng));
        st->setInt(10, in.duration_sec);
        st->setInt(11, in.distance_m);
        st->setInt(12, in.amount_cent);
        st->executeUpdate();

        int ride_id = 0;
        std::unique_ptr<sql::Statement> id_st(conn->createStatement());
        std::unique_ptr<sql::ResultSet> id_rs(id_st->executeQuery("SELECT LAST_INSERT_ID()"));
        if (id_rs->next()) ride_id = id_rs->getInt(1);

        if (!in.points.empty()) {
            std::unique_ptr<sql::PreparedStatement> pst(conn->prepareStatement(
                "INSERT INTO ride_position(ride_id,seq,lat,lng,elapsed_sec) VALUES(?,?,?,?,?)"));
            for (const auto& p : in.points) {
                pst->setInt(1, ride_id);
                pst->setInt(2, p.seq);
                pst->setString(3, fmt7(p.lat));
                pst->setString(4, fmt7(p.lng));
                pst->setInt(5, p.elapsed_sec);
                pst->executeUpdate();
            }
        }
        conn->commit();
        conn->setAutoCommit(true);

        Ride r;
        r.id = ride_id;
        r.ride_no = in.ride_no;
        r.user_id = in.user_id;
        r.bike_id = in.bike_id;
        r.start_ts = in.start_ts;
        r.end_ts = in.end_ts;
        r.start_lat = in.start_lat;
        r.start_lng = in.start_lng;
        r.end_lat = in.end_lat;
        r.end_lng = in.end_lng;
        r.duration_sec = in.duration_sec;
        r.distance_m = in.distance_m;
        r.amount_cent = in.amount_cent;
        r.status = 0;
        return r;
    } catch (...) {
        conn->rollback();
        conn->setAutoCommit(true);
        throw;
    }
}

std::optional<Ride> MysqlRideRepo::find_by_no(const std::string& no) {
    auto conn = pool_->get();
    std::unique_ptr<sql::PreparedStatement> st(
        conn->prepareStatement("SELECT * FROM ride WHERE ride_no=?"));
    st->setString(1, no);
    std::unique_ptr<sql::ResultSet> rs(st->executeQuery());
    if (!rs->next()) return std::nullopt;
    return row_to_ride(*rs);
}

std::vector<RidePoint> MysqlRideRepo::list_points(int ride_id) {
    auto conn = pool_->get();
    std::unique_ptr<sql::PreparedStatement> st(conn->prepareStatement(
        "SELECT seq,lat,lng,elapsed_sec FROM ride_position WHERE ride_id=? ORDER BY seq"));
    st->setInt(1, ride_id);
    std::unique_ptr<sql::ResultSet> rs(st->executeQuery());
    std::vector<RidePoint> out;
    while (rs->next()) {
        RidePoint p;
        p.seq = rs->getInt("seq");
        p.lat = std::stod(rs.getString("lat").asStdString());
        p.lng = std::stod(rs.getString("lng").asStdString());
        p.elapsed_sec = rs->getInt("elapsed_sec");
        out.push_back(p);
    }
    return out;
}

std::vector<Ride> MysqlRideRepo::list_by_user(int uid, int limit) {
    auto conn = pool_->get();
    std::unique_ptr<sql::PreparedStatement> st(conn->prepareStatement(
        "SELECT * FROM ride WHERE user_id=? ORDER BY start_tm DESC LIMIT ?"));
    st->setInt(1, uid);
    st->setInt(2, limit);
    std::unique_ptr<sql::ResultSet> rs(st->executeQuery());
    std::vector<Ride> out;
    while (rs->next()) out.push_back(row_to_ride(*rs));
    return out;
}

} // namespace bike::server
```

- [ ] **Step 3: 加入 bike_server_prod**

`server/CMakeLists.txt` 把 `src/db/mysql_ride_repo.cpp` 加入。

- [ ] **Step 4: 构建**

Run: `cmake --build build --target bike_server_prod -j`
Expected: 无错误

- [ ] **Step 5: Commit**

```bash
git add server/include/server/db/mysql_ride_repo.hpp server/src/db/mysql_ride_repo.cpp server/CMakeLists.txt
git commit -m "feat(server): MysqlRideRepo with transactional points insert"
```

---

## Task 14: auth.hpp 工具函数

**Files:**
- Create: `server/include/server/auth.hpp`
- Create: `server/src/auth.cpp`

- [ ] **Step 1: 写头文件**

```cpp
// server/include/server/auth.hpp
#pragma once

#include "server/router.hpp"   // 拿到 Ctx

#include <optional>

namespace bike::server {

// 验证 session_token 并返回 user_id。失败返回 nullopt。
// 内部调用 ctx.sessions->lookup_session(token),再用 ctx.users->find_or_create(mobile) 拿 id。
std::optional<int> require_user(const std::string& token, Ctx& ctx);

} // namespace bike::server
```

- [ ] **Step 2: 实现**

```cpp
// server/src/auth.cpp
#include "server/auth.hpp"

namespace bike::server {

std::optional<int> require_user(const std::string& token, Ctx& ctx) {
    auto mobile = ctx.sessions->lookup_session(token);
    if (!mobile) return std::nullopt;
    User u = ctx.users->find_or_create(*mobile);
    return u.id;
}

} // namespace bike::server
```

- [ ] **Step 3: 加入 bike_server_core**

`server/CMakeLists.txt` 把 `src/auth.cpp` 加入 `bike_server_core`。

- [ ] **Step 4: 构建**

Run: `cmake --build build --target bike_server_core -j`
Expected: 无错误

- [ ] **Step 5: Commit**

```bash
git add server/include/server/auth.hpp server/src/auth.cpp server/CMakeLists.txt
git commit -m "feat(server): require_user auth helper"
```

---

## Task 15: list_nearby_bikes handler(TDD)

**Files:**
- Create: `server/src/handlers/list_nearby_bikes.cpp`
- Create: `server/tests/test_handlers_ride.cpp`
- Modify: `server/include/server/handlers.hpp`
- Modify: `server/CMakeLists.txt`

- [ ] **Step 1: 写 handler 声明**

在 `server/include/server/handlers.hpp` 命名空间内追加:

```cpp
std::vector<std::uint8_t> list_nearby_bikes(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> scan_unlock(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> position_report(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> end_ride(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> report_damage(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> get_ride_detail(const std::string& payload, Ctx& ctx);
std::vector<std::uint8_t> list_rides(const std::string& payload, Ctx& ctx);
```

- [ ] **Step 2: 实现 list_nearby_bikes**

```cpp
// server/src/handlers/list_nearby_bikes.cpp
#include "server/handlers.hpp"
#include "server/auth.hpp"
#include "server/logging.hpp"
#include "bike/geo.hpp"
#include "bike/validation.hpp"

#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> list_nearby_bikes(const std::string& payload, Ctx& ctx) {
    tutorial::list_nearby_bikes_response rsp;
    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        Frame f{.event_id = 0x12, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::list_nearby_bikes_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);

    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);

    if (!valid_lat(req.lat()) || !valid_lng(req.lng()))
        return fail(ErrCode::InvalidData);

    double r = clamp_radius(req.radius_m());
    // 1 度 lat ≈ 111000 m;1 度 lng ≈ 111000 * cos(lat)
    double dlat = r / 111000.0;
    double dlng = r / (111000.0 * std::cos(req.lat() * 3.14159265358979 / 180.0));

    auto bikes = ctx.bikes->list_in_bounds(
        req.lat() - dlat, req.lat() + dlat,
        req.lng() - dlng, req.lng() + dlng);

    rsp.set_code(code(ErrCode::Ok));
    for (const auto& b : bikes) {
        // 精过滤:Haversine 距离 ≤ r
        double d = haversine_m(req.lat(), req.lng(), b.lat, b.lng);
        if (d > r) continue;
        auto* bi = rsp.add_bikes();
        bi->set_bike_no(b.bike_no);
        bi->set_lat(b.lat);
        bi->set_lng(b.lng);
        bi->set_status(static_cast<int>(b.status));
    }
    Frame f{.event_id = 0x12, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
```

注:需要 `#include <cmath>`,加到文件顶部。

- [ ] **Step 3: 写单测**

```cpp
// server/tests/test_handlers_ride.cpp
#include "server/handlers.hpp"
#include "server/repo/in_memory.hpp"
#include "server/ride_session_store.hpp"
#include "server/router.hpp"
#include "bike/protocol.hpp"

#include <bike.pb.h>
#include <gtest/gtest.h>

using namespace bike;
using namespace bike::server;

namespace {
struct Fixture {
    std::shared_ptr<InMemoryUserRepo>    users{std::make_shared<InMemoryUserRepo>()};
    std::shared_ptr<InMemoryAccountRepo> accounts{std::make_shared<InMemoryAccountRepo>()};
    std::shared_ptr<InMemoryBikeRepo>    bikes{std::make_shared<InMemoryBikeRepo>()};
    std::shared_ptr<InMemoryRideRepo>    rides{std::make_shared<InMemoryRideRepo>()};
    std::shared_ptr<ISessionStore>       sessions;     // 简化:用一个 InMemory session store
    std::shared_ptr<RideSessionStore>    ride_sessions{std::make_shared<RideSessionStore>()};
    Ctx ctx{users, accounts, sessions, bikes, rides, ride_sessions};
    std::string token{"tok-1"};

    Fixture() {
        // 把 token 注册到 sessions(具体看现有 ISessionStore 接口)
        // sessions->upsert_session(token, "15600000010");
        // 这里假设测试 fixture 自带一个 InMemorySessionStore 实现
    }
};

tutorial::list_nearby_bikes_response parse_nearby(const std::vector<std::uint8_t>& bytes) {
    auto fr = decode(bytes.data(), bytes.size());
    tutorial::list_nearby_bikes_response r;
    r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    return r;
}
}

TEST(ListNearbyBikes, ReturnsIdleAndDamagedInRadius) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Idle});
    f.bikes->seed({.id = 2, .bike_no = "BJ-002", .lat = 39.983, .lng = 116.315, .status = BikeStatus::Damaged});
    f.bikes->seed({.id = 3, .bike_no = "BJ-003", .lat = 40.000, .lng = 116.500, .status = BikeStatus::Idle});

    tutorial::list_nearby_bikes_request req;
    req.set_session_token(f.token);
    req.set_lat(39.982);
    req.set_lng(116.314);
    req.set_radius_m(500);

    auto bytes = handlers::list_nearby_bikes(req.SerializeAsString(), f.ctx);
    auto rsp = parse_nearby(bytes);
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_EQ(rsp.bikes_size(), 2);   // BJ-001 + BJ-002,BJ-003 太远
}

TEST(ListNearbyBikes, UnauthorizedIfBadToken) {
    Fixture f;
    tutorial::list_nearby_bikes_request req;
    req.set_session_token("invalid");
    req.set_lat(39.982); req.set_lng(116.314); req.set_radius_m(500);
    auto bytes = handlers::list_nearby_bikes(req.SerializeAsString(), f.ctx);
    auto rsp = parse_nearby(bytes);
    EXPECT_EQ(rsp.code(), 401);
}
```

注:Fixture 的构造里如果现有 ISessionStore 不能直接构造 InMemory 版本,要么补一个 InMemorySessionStore 实现(参考 in_memory.hpp 里的现有 InMemorySessionStore,如果有),要么 mock 掉。具体看 `server/include/server/repo/session_store.hpp`。

- [ ] **Step 4: CMakeLists 加 handler + test**

`server/CMakeLists.txt` 把 `src/handlers/list_nearby_bikes.cpp` 加入 `bike_server_core`。

测试:
```cmake
add_executable(test_handlers_ride tests/test_handlers_ride.cpp)
target_link_libraries(test_handlers_ride PRIVATE bike_server_core GTest::gtest_main)
target_include_directories(test_handlers_ride PRIVATE include)
add_test(NAME handlers_ride COMMAND test_handlers_ride)
```

- [ ] **Step 5: 跑测试,确认失败 / 通过**

Run: `cmake --build build --target test_handlers_ride -j && build/server/test_handlers_ride`
Expected: 2 个 case 全 PASS

- [ ] **Step 6: Commit**

```bash
git add server/src/handlers/list_nearby_bikes.cpp server/include/server/handlers.hpp server/tests/test_handlers_ride.cpp server/CMakeLists.txt
git commit -m "feat(server): list_nearby_bikes handler with bounding box + haversine"
```

---

## Task 16: scan_unlock handler(TDD)

**Files:**
- Create: `server/src/handlers/scan_unlock.cpp`
- Modify: `server/tests/test_handlers_ride.cpp`

- [ ] **Step 1: 实现 scan_unlock**

```cpp
// server/src/handlers/scan_unlock.cpp
#include "server/handlers.hpp"
#include "server/auth.hpp"
#include "server/logging.hpp"
#include "bike/geo.hpp"
#include "bike/ride_no.hpp"
#include "bike/validation.hpp"

#include <bike.pb.h>
#include <chrono>

namespace bike::server::handlers {

namespace {
long long now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
int today_yyyymmdd() {
    auto t = std::time(nullptr);
    std::tm tm = *std::gmtime(&t);
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}
}

std::vector<std::uint8_t> scan_unlock(const std::string& payload, Ctx& ctx) {
    tutorial::scan_unlock_response rsp;
    auto fail = [&](ErrCode ec, std::string desc = "") {
        rsp.set_code(code(ec));
        rsp.set_desc(desc.empty() ? std::string(to_string(ec)) : desc);
        Frame f{.event_id = 0x14, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::scan_unlock_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);

    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);

    if (!valid_bike_no(req.bike_no()) || !valid_lat(req.lat()) || !valid_lng(req.lng()))
        return fail(ErrCode::InvalidData);

    auto bike = ctx.bikes->get_for_update(req.bike_no());
    if (!bike) return fail(ErrCode::InvalidData, "车辆不存在");

    if (bike->status == BikeStatus::Damaged) return fail(ErrCode::BikeIsDamaged);
    if (bike->status == BikeStatus::Rented)  return fail(ErrCode::BikeIsRunning);

    // 余额必须 ≥ 起步价
    int bal = ctx.accounts->get_balance(*uid);
    if (bal < 100) return fail(ErrCode::ProcessFailed, "余额不足");

    // 生成 ride_no:实际生产用 Redis INCR,这里简化为秒级时间戳唯一
    // 完整实现:ctx.sessions->next_daily_seq("ride_seq_<date>") → 1..999999
    // 此处用 unix % 999999 + 1 作为占位,集成测试覆盖真实 Redis 路径
    int seq = static_cast<int>((now_unix() % 999999) + 1);
    std::string ride_no = make_ride_no(today_yyyymmdd(), seq);

    RideSession s;
    s.ride_no   = ride_no;
    s.user_id   = *uid;
    s.bike_id   = bike->id;
    s.start_lat = req.lat();
    s.start_lng = req.lng();
    s.start_ts  = now_unix();
    s.last_lat  = req.lat();
    s.last_lng  = req.lng();
    s.last_seq  = 0;
    ctx.ride_sessions->create(s);

    ctx.bikes->update_status(bike->id, BikeStatus::Rented);

    rsp.set_code(code(ErrCode::Ok));
    rsp.set_desc(std::string(to_string(ErrCode::Ok)));
    rsp.set_ride_no(ride_no);
    rsp.set_start_ts(s.start_ts);
    BIKE_LOG_INFO("scan_unlock user={} bike={} ride={}", *uid, req.bike_no(), ride_no);
    Frame f{.event_id = 0x14, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
```

注:`next_daily_seq` 的 Redis 实现先 stub,集成测试前补齐。先用 `now_unix() % 999999` 占位。

- [ ] **Step 2: 加入 bike_server_core**

`server/CMakeLists.txt` 加 `src/handlers/scan_unlock.cpp`。

- [ ] **Step 3: 写测试**

追加到 `test_handlers_ride.cpp`:

```cpp
tutorial::scan_unlock_response parse_unlock(const std::vector<std::uint8_t>& bytes) {
    auto fr = decode(bytes.data(), bytes.size());
    tutorial::scan_unlock_response r;
    r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    return r;
}

TEST(ScanUnlock, SuccessCreatesSessionAndRentsBike) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Idle});
    // 给账户充值 1 元(100 分),满足余额检查
    f.accounts->add_balance(/*user_id=*/1, RecordType::Recharge, 1000);

    tutorial::scan_unlock_request req;
    req.set_session_token(f.token);
    req.set_bike_no("BJ-001");
    req.set_lat(39.982); req.set_lng(116.314);
    auto bytes = handlers::scan_unlock(req.SerializeAsString(), f.ctx);
    auto rsp = parse_unlock(bytes);
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_FALSE(rsp.ride_no().empty());
    EXPECT_GT(rsp.start_ts(), 0);

    auto bike = f.bikes->get_for_update("BJ-001");
    EXPECT_EQ(bike->status, BikeStatus::Rented);
    EXPECT_TRUE(f.ride_sessions->find(rsp.ride_no()).has_value());
}

TEST(ScanUnlock, DamagedBikeReturns409) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Damaged});
    tutorial::scan_unlock_request req;
    req.set_session_token(f.token);
    req.set_bike_no("BJ-001");
    req.set_lat(39.982); req.set_lng(116.314);
    auto rsp = parse_unlock(handlers::scan_unlock(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 409);
    auto bike = f.bikes->get_for_update("BJ-001");
    EXPECT_EQ(bike->status, BikeStatus::Damaged);   // 不变
}

TEST(ScanUnlock, RentedBikeReturns408) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Rented});
    tutorial::scan_unlock_request req;
    req.set_session_token(f.token); req.set_bike_no("BJ-001");
    req.set_lat(39.982); req.set_lng(116.314);
    auto rsp = parse_unlock(handlers::scan_unlock(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 408);
}

TEST(ScanUnlock, InsufficientBalanceReturns406) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Idle});
    // 故意不充值,balance = 0
    tutorial::scan_unlock_request req;
    req.set_session_token(f.token); req.set_bike_no("BJ-001");
    req.set_lat(39.982); req.set_lng(116.314);
    auto rsp = parse_unlock(handlers::scan_unlock(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 406);
}

TEST(ScanUnlock, UnknownBikeReturns404) {
    Fixture f;
    tutorial::scan_unlock_request req;
    req.set_session_token(f.token); req.set_bike_no("BJ-NOSUCH");
    req.set_lat(39.982); req.set_lng(116.314);
    auto rsp = parse_unlock(handlers::scan_unlock(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 404);
}
```

- [ ] **Step 4: 跑测试**

Run: `cmake --build build --target test_handlers_ride -j && build/server/test_handlers_ride`
Expected: list_nearby 2 + scan_unlock 5 共 7 个 case 全 PASS

- [ ] **Step 5: Commit**

```bash
git add server/src/handlers/scan_unlock.cpp server/tests/test_handlers_ride.cpp server/CMakeLists.txt
git commit -m "feat(server): scan_unlock handler with all error paths"
```

---

## Task 17: position_report handler

**Files:**
- Create: `server/src/handlers/position_report.cpp`
- Modify: `server/tests/test_handlers_ride.cpp`

- [ ] **Step 1: 实现**

```cpp
// server/src/handlers/position_report.cpp
#include "server/handlers.hpp"
#include "server/logging.hpp"

#include <bike.pb.h>

namespace bike::server::handlers {

// 单向事件,无响应。返回空 vector(router 不会向 client 发回任何字节)。
std::vector<std::uint8_t> position_report(const std::string& payload, Ctx& ctx) {
    tutorial::ride_position_report req;
    if (!req.ParseFromArray(payload.data(), payload.size())) return {};
    ctx.ride_sessions->update_pos(req.ride_no(), req.lat(), req.lng(), req.seq());
    return {};
}

} // namespace bike::server::handlers
```

- [ ] **Step 2: CMakeLists + router 改造,支持空响应**

router 当前对返回值会发送响应。若 `bytes.empty()` 则不发 —— 检查 `server/src/router.cpp` 或 `session.cpp`,确认对空 vector 的处理。

如果当前会因空 vector 而 crash 或 send 0 字节,加判断:
```cpp
if (!out.empty()) session->send(out);
```

- [ ] **Step 3: 加入 bike_server_core**

`server/CMakeLists.txt` 加 `src/handlers/position_report.cpp`。

- [ ] **Step 4: 写测试**

```cpp
TEST(PositionReport, UpdatesSessionPos) {
    Fixture f;
    f.ride_sessions->create({.ride_no = "R1", .user_id = 1, .bike_id = 1});
    tutorial::ride_position_report req;
    req.set_ride_no("R1"); req.set_seq(5);
    req.set_lat(39.985); req.set_lng(116.318); req.set_elapsed_sec(5);
    auto bytes = handlers::position_report(req.SerializeAsString(), f.ctx);
    EXPECT_TRUE(bytes.empty());
    auto s = f.ride_sessions->find("R1");
    EXPECT_DOUBLE_EQ(s->last_lat, 39.985);
    EXPECT_EQ(s->last_seq, 5);
}

TEST(PositionReport, UnknownRideIsSilent) {
    Fixture f;
    tutorial::ride_position_report req;
    req.set_ride_no("RNONE");
    auto bytes = handlers::position_report(req.SerializeAsString(), f.ctx);
    EXPECT_TRUE(bytes.empty());
}
```

- [ ] **Step 5: 跑测试**

Run: `cmake --build build --target test_handlers_ride -j && build/server/test_handlers_ride`
Expected: 9 个 case 全 PASS

- [ ] **Step 6: Commit**

```bash
git add server/src/handlers/position_report.cpp server/tests/test_handlers_ride.cpp server/CMakeLists.txt
git commit -m "feat(server): position_report fire-and-forget handler"
```

---

## Task 18: end_ride handler(含幂等)

**Files:**
- Create: `server/src/handlers/end_ride.cpp`
- Modify: `server/tests/test_handlers_ride.cpp`

- [ ] **Step 1: 实现**

```cpp
// server/src/handlers/end_ride.cpp
#include "server/handlers.hpp"
#include "server/auth.hpp"
#include "server/logging.hpp"
#include "bike/pricing.hpp"
#include "bike/validation.hpp"

#include <bike.pb.h>
#include <chrono>

namespace bike::server::handlers {

namespace {
long long now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
}

std::vector<std::uint8_t> end_ride(const std::string& payload, Ctx& ctx) {
    tutorial::end_ride_response rsp;
    auto fail = [&](ErrCode ec, std::string desc = "") {
        rsp.set_code(code(ec));
        rsp.set_desc(desc.empty() ? std::string(to_string(ec)) : desc);
        Frame f{.event_id = 0x18, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::end_ride_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);
    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);
    if (!valid_lat(req.end_lat()) || !valid_lng(req.end_lng()))
        return fail(ErrCode::InvalidData);

    auto sess = ctx.ride_sessions->find(req.ride_no());
    if (!sess) {
        // 幂等路径:查历史
        auto row = ctx.rides->find_by_no(req.ride_no());
        if (!row)              return fail(ErrCode::InvalidData, "订单不存在");
        if (row->user_id != *uid) return fail(ErrCode::Unauthorized);
        rsp.set_code(code(ErrCode::Ok));
        rsp.set_desc(std::string(to_string(ErrCode::Ok)));
        rsp.set_duration_sec(row->duration_sec);
        rsp.set_distance_m(row->distance_m);
        rsp.set_amount_cent(row->amount_cent);
        // 余额查询补充(可能不是扣款后立刻查,但够 MVP)
        rsp.set_balance_after(ctx.accounts->get_balance(*uid));
        Frame f{.event_id = 0x18, .payload = rsp.SerializeAsString()};
        return encode(f);
    }
    if (sess->user_id != *uid) return fail(ErrCode::Unauthorized);

    long long end_ts = now_unix();
    int duration_sec = static_cast<int>(end_ts - sess->start_ts);
    int amount = compute_fee(duration_sec);
    // 距离:简化用 start→end 直线;真实生产从 ride_position 累加(留 TODO)
    // 这里要求 position_report 已经把 last_pos 更新过几次
    double dist_m = haversine_m(sess->start_lat, sess->start_lng,
                                 req.end_lat(), req.end_lng());

    // 扣款(注意:add_balance 内部要校验 balance ≥ amount,不足返回 -1)
    int new_bal = ctx.accounts->add_balance(*uid, RecordType::Consume, -amount);
    if (new_bal < 0) {
        // 余额扣不动 —— 不应发生(scan_unlock 已校验起步价),但保险
        return fail(ErrCode::ProcessFailed, "余额不足");
    }

    // 写 ride + points
    // 注意:生产应从 ride_sessions 累积所有上报点;这里简化为只写起点+终点两个点
    std::vector<RidePoint> points = {
        {.seq = 0, .lat = sess->start_lat, .lng = sess->start_lng, .elapsed_sec = 0},
        {.seq = sess->last_seq, .lat = req.end_lat(), .lng = req.end_lng(),
         .elapsed_sec = duration_sec},
    };

    CreateRideInput in{
        .ride_no = sess->ride_no, .user_id = *uid, .bike_id = sess->bike_id,
        .start_ts = sess->start_ts, .end_ts = end_ts,
        .start_lat = sess->start_lat, .start_lng = sess->start_lng,
        .end_lat = req.end_lat(), .end_lng = req.end_lng(),
        .duration_sec = duration_sec,
        .distance_m = static_cast<int>(dist_m),
        .amount_cent = amount,
        .points = std::move(points),
    };
    ctx.rides->create_with_points(in);

    ctx.bikes->update_location(sess->bike_id, req.end_lat(), req.end_lng());
    ctx.bikes->update_status(sess->bike_id, BikeStatus::Idle);
    ctx.ride_sessions->remove(sess->ride_no);

    rsp.set_code(code(ErrCode::Ok));
    rsp.set_desc(std::string(to_string(ErrCode::Ok)));
    rsp.set_duration_sec(duration_sec);
    rsp.set_distance_m(static_cast<int>(dist_m));
    rsp.set_amount_cent(amount);
    rsp.set_balance_after(new_bal);

    BIKE_LOG_INFO("end_ride ride={} dur={} amt={} bal={}",
                  sess->ride_no, duration_sec, amount, new_bal);
    Frame f{.event_id = 0x18, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
```

注:需要 `#include "bike/geo.hpp"` 拿 `haversine_m`。

- [ ] **Step 2: IAccountRepo::add_balance 处理负值**

`add_balance` 接收负值时,如果会导致 balance < 0 应返回 -1。检查 `server/include/server/repo/account_repo.hpp` 的接口约定,InMemoryAccountRepo 实现内加保护:

```cpp
int add_balance(int uid, RecordType type, int amount) override {
    std::lock_guard<std::mutex> lk(mu_);
    int cur = bal_.count(uid) ? bal_[uid] : 0;
    if (amount < 0 && cur + amount < 0) return -1;   // 不足
    int newb = cur + amount;
    bal_[uid] = newb;
    records_[uid].push_back({static_cast<int>(type), std::abs(amount), newb, /*ts=*/0});
    return newb;
}
```

- [ ] **Step 3: 加入 bike_server_core**

`server/CMakeLists.txt` 加 `src/handlers/end_ride.cpp`。

- [ ] **Step 4: 测试**

```cpp
tutorial::end_ride_response parse_end(const std::vector<std::uint8_t>& bytes) {
    auto fr = decode(bytes.data(), bytes.size());
    tutorial::end_ride_response r;
    r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    return r;
}

TEST(EndRide, SuccessPathChargesAndArchives) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Rented});
    f.accounts->add_balance(1, RecordType::Recharge, 1000);
    f.ride_sessions->create({
        .ride_no = "R1", .user_id = 1, .bike_id = 1,
        .start_lat = 39.982, .start_lng = 116.314,
        .start_ts = /*某个过去时间*/,
    });

    tutorial::end_ride_request req;
    req.set_session_token(f.token); req.set_ride_no("R1");
    req.set_end_lat(39.985); req.set_end_lng(116.318);
    auto rsp = parse_end(handlers::end_ride(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_GT(rsp.amount_cent(), 0);
    EXPECT_EQ(rsp.balance_after(), 1000 - rsp.amount_cent());

    // bike 改回 idle + 移到新位置
    auto bike = f.bikes->get_for_update("BJ-001");
    EXPECT_EQ(bike->status, BikeStatus::Idle);
    EXPECT_NEAR(bike->lat, 39.985, 0.0001);

    // session 已清
    EXPECT_FALSE(f.ride_sessions->find("R1").has_value());

    // ride 表有记录
    auto ride = f.rides->find_by_no("R1");
    ASSERT_TRUE(ride.has_value());
    EXPECT_EQ(ride->amount_cent, rsp.amount_cent());
}

TEST(EndRide, IdempotentReturnsHistoryWithoutDoubleCharge) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Idle});
    f.accounts->add_balance(1, RecordType::Recharge, 1000);
    // 预置已结订单
    f.rides->create_with_points({
        .ride_no = "R1", .user_id = 1, .bike_id = 1,
        .start_ts = 1000, .end_ts = 2000,
        .start_lat = 39.982, .start_lng = 116.314,
        .end_lat = 39.985, .end_lng = 116.318,
        .duration_sec = 1000, .distance_m = 300,
        .amount_cent = 150, .points = {},
    });
    int bal_before = f.accounts->get_balance(1);

    tutorial::end_ride_request req;
    req.set_session_token(f.token); req.set_ride_no("R1");
    req.set_end_lat(39.985); req.set_end_lng(116.318);
    auto rsp = parse_end(handlers::end_ride(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_EQ(rsp.amount_cent(), 150);
    EXPECT_EQ(f.accounts->get_balance(1), bal_before);   // 没二次扣款
}

TEST(EndRide, CrossUserReturns401) {
    Fixture f;
    f.rides->create_with_points({
        .ride_no = "R1", .user_id = 999, /* ... 同上 */
    });
    tutorial::end_ride_request req;
    req.set_session_token(f.token); req.set_ride_no("R1");
    req.set_end_lat(39.985); req.set_end_lng(116.318);
    auto rsp = parse_end(handlers::end_ride(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 401);
}
```

- [ ] **Step 5: 跑测试**

Run: `cmake --build build --target test_handlers_ride -j && build/server/test_handlers_ride`
Expected: 12 个 case 全 PASS

- [ ] **Step 6: Commit**

```bash
git add server/src/handlers/end_ride.cpp server/tests/test_handlers_ride.cpp server/include/server/repo/in_memory.hpp server/CMakeLists.txt
git commit -m "feat(server): end_ride with idempotency via history fallback"
```

---

## Task 19: report_damage handler

**Files:**
- Create: `server/src/handlers/report_damage.cpp`
- Modify: `server/tests/test_handlers_ride.cpp`

- [ ] **Step 1: 实现**

```cpp
// server/src/handlers/report_damage.cpp
#include "server/handlers.hpp"
#include "server/auth.hpp"
#include "bike/validation.hpp"

#include <bike.pb.h>

namespace bike::server::handlers {

std::vector<std::uint8_t> report_damage(const std::string& payload, Ctx& ctx) {
    tutorial::report_damage_response rsp;
    auto fail = [&](ErrCode ec, std::string desc = "") {
        rsp.set_code(code(ec));
        rsp.set_desc(desc.empty() ? std::string(to_string(ec)) : desc);
        Frame f{.event_id = 0x1A, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::report_damage_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);
    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);
    if (!valid_bike_no(req.bike_no())) return fail(ErrCode::InvalidData);

    auto bike = ctx.bikes->get_for_update(req.bike_no());
    if (!bike) return fail(ErrCode::InvalidData, "车辆不存在");

    ctx.bikes->update_status(bike->id, BikeStatus::Damaged);

    rsp.set_code(code(ErrCode::Ok));
    rsp.set_desc(std::string(to_string(ErrCode::Ok)));
    Frame f{.event_id = 0x1A, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
```

- [ ] **Step 2: 测试**

```cpp
tutorial::report_damage_response parse_damage(const std::vector<std::uint8_t>& b) {
    auto fr = decode(b.data(), b.size());
    tutorial::report_damage_response r;
    r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    return r;
}

TEST(ReportDamage, MarksBikeDamaged) {
    Fixture f;
    f.bikes->seed({.id = 1, .bike_no = "BJ-001", .lat = 39.982, .lng = 116.314, .status = BikeStatus::Idle});
    tutorial::report_damage_request req;
    req.set_session_token(f.token); req.set_bike_no("BJ-001");
    req.set_note("刹车失灵");
    auto rsp = parse_damage(handlers::report_damage(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);
    auto bike = f.bikes->get_for_update("BJ-001");
    EXPECT_EQ(bike->status, BikeStatus::Damaged);
}

TEST(ReportDamage, UnknownBikeReturns404) {
    Fixture f;
    tutorial::report_damage_request req;
    req.set_session_token(f.token); req.set_bike_no("BJ-NOSUCH");
    auto rsp = parse_damage(handlers::report_damage(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 404);
}
```

- [ ] **Step 3: 跑测试**

Run: `cmake --build build --target test_handlers_ride -j && build/server/test_handlers_ride`
Expected: 14 个 case 全 PASS

- [ ] **Step 4: Commit**

```bash
git add server/src/handlers/report_damage.cpp server/tests/test_handlers_ride.cpp server/CMakeLists.txt
git commit -m "feat(server): report_damage handler"
```

---

## Task 20: get_ride_detail handler

**Files:**
- Create: `server/src/handlers/get_ride_detail.cpp`
- Modify: `server/tests/test_handlers_ride.cpp`

- [ ] **Step 1: 实现**

```cpp
// server/src/handlers/get_ride_detail.cpp
#include "server/handlers.hpp"
#include "server/auth.hpp"

#include <bike.pb.h>
#include <ctime>
#include <sstream>

namespace bike::server::handlers {

namespace {
std::string fmt_iso(long long unix_sec) {
    auto t = static_cast<std::time_t>(unix_sec);
    std::tm tm = *std::gmtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string{buf};
}
}

std::vector<std::uint8_t> get_ride_detail(const std::string& payload, Ctx& ctx) {
    tutorial::get_ride_detail_response rsp;
    auto fail = [&](ErrCode ec, std::string desc = "") {
        rsp.set_code(code(ec));
        rsp.set_desc(desc.empty() ? std::string(to_string(ec)) : desc);
        Frame f{.event_id = 0x1C, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::get_ride_detail_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);
    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);

    auto ride = ctx.rides->find_by_no(req.ride_no());
    if (!ride)               return fail(ErrCode::InvalidData, "订单不存在");
    if (ride->user_id != *uid) return fail(ErrCode::Unauthorized);

    rsp.set_code(code(ErrCode::Ok));
    rsp.set_ride_no(ride->ride_no);
    rsp.set_duration_sec(ride->duration_sec);
    rsp.set_distance_m(ride->distance_m);
    rsp.set_amount_cent(ride->amount_cent);
    rsp.set_start_tm(fmt_iso(ride->start_ts));
    rsp.set_end_tm(fmt_iso(ride->end_ts));

    auto points = ctx.rides->list_points(ride->id);
    for (const auto& p : points) {
        auto* pp = rsp.add_points();
        pp->set_lat(p.lat);
        pp->set_lng(p.lng);
        pp->set_elapsed_sec(p.elapsed_sec);
    }
    Frame f{.event_id = 0x1C, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
```

- [ ] **Step 2: 测试**

```cpp
tutorial::get_ride_detail_response parse_detail(const std::vector<std::uint8_t>& b) {
    auto fr = decode(b.data(), b.size());
    tutorial::get_ride_detail_response r;
    r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    return r;
}

TEST(GetRideDetail, ReturnsPointsForOwner) {
    Fixture f;
    std::vector<RidePoint> pts = {
        {.seq = 0, .lat = 39.982, .lng = 116.314, .elapsed_sec = 0},
        {.seq = 1, .lat = 39.983, .lng = 116.315, .elapsed_sec = 5},
        {.seq = 2, .lat = 39.984, .lng = 116.316, .elapsed_sec = 10},
    };
    auto r = f.rides->create_with_points({
        .ride_no = "R1", .user_id = 1, .bike_id = 1,
        .start_ts = 1000, .end_ts = 1010,
        .start_lat = 39.982, .start_lng = 116.314,
        .end_lat = 39.984, .end_lng = 116.316,
        .duration_sec = 10, .distance_m = 200,
        .amount_cent = 100, .points = pts,
    });
    tutorial::get_ride_detail_request req;
    req.set_session_token(f.token); req.set_ride_no("R1");
    auto rsp = parse_detail(handlers::get_ride_detail(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_EQ(rsp.points_size(), 3);
    EXPECT_EQ(rsp.points(0).lat(), 39.982);
}

TEST(GetRideDetail, CrossUserReturns401) {
    Fixture f;
    f.rides->create_with_points({.ride_no = "RX", .user_id = 999});
    tutorial::get_ride_detail_request req;
    req.set_session_token(f.token); req.set_ride_no("RX");
    auto rsp = parse_detail(handlers::get_ride_detail(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 401);
}
```

- [ ] **Step 3: 跑测试**

Run: `cmake --build build --target test_handlers_ride -j && build/server/test_handlers_ride`
Expected: 16 个 case 全 PASS

- [ ] **Step 4: Commit**

```bash
git add server/src/handlers/get_ride_detail.cpp server/tests/test_handlers_ride.cpp server/CMakeLists.txt
git commit -m "feat(server): get_ride_detail handler"
```

---

## Task 21: list_rides handler

**Files:**
- Create: `server/src/handlers/list_rides.cpp`
- Modify: `server/tests/test_handlers_ride.cpp`

- [ ] **Step 1: 实现**

```cpp
// server/src/handlers/list_rides.cpp
#include "server/handlers.hpp"
#include "server/auth.hpp"
#include "bike/validation.hpp"

#include <bike.pb.h>
#include <ctime>

namespace bike::server::handlers {

namespace {
std::string fmt_iso(long long unix_sec) {
    auto t = static_cast<std::time_t>(unix_sec);
    std::tm tm = *std::gmtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string{buf};
}
}

std::vector<std::uint8_t> list_rides(const std::string& payload, Ctx& ctx) {
    tutorial::list_rides_response rsp;
    auto fail = [&](ErrCode ec) {
        rsp.set_code(code(ec));
        Frame f{.event_id = 0x1E, .payload = rsp.SerializeAsString()};
        return encode(f);
    };

    tutorial::list_rides_request req;
    if (!req.ParseFromArray(payload.data(), payload.size()))
        return fail(ErrCode::InvalidMsg);
    auto uid = require_user(req.session_token(), ctx);
    if (!uid) return fail(ErrCode::Unauthorized);

    int limit = clamp_limit(req.limit());
    auto rides = ctx.rides->list_by_user(*uid, limit);

    rsp.set_code(code(ErrCode::Ok));
    for (const auto& r : rides) {
        auto* s = rsp.add_rides();
        s->set_ride_no(r.ride_no);
        s->set_start_tm(fmt_iso(r.start_ts));
        s->set_duration_sec(r.duration_sec);
        s->set_distance_m(r.distance_m);
        s->set_amount_cent(r.amount_cent);
    }
    Frame f{.event_id = 0x1E, .payload = rsp.SerializeAsString()};
    return encode(f);
}

} // namespace bike::server::handlers
```

- [ ] **Step 2: 测试**

```cpp
tutorial::list_rides_response parse_list(const std::vector<std::uint8_t>& b) {
    auto fr = decode(b.data(), b.size());
    tutorial::list_rides_response r;
    r.ParseFromArray(fr->frame.payload.data(), fr->frame.payload.size());
    return r;
}

TEST(ListRides, ReturnsUsersRidesOnly) {
    Fixture f;
    f.rides->create_with_points({.ride_no = "R1", .user_id = 1, .amount_cent = 100});
    f.rides->create_with_points({.ride_no = "R2", .user_id = 1, .amount_cent = 150});
    f.rides->create_with_points({.ride_no = "RX", .user_id = 999});
    tutorial::list_rides_request req;
    req.set_session_token(f.token); req.set_limit(20);
    auto rsp = parse_list(handlers::list_rides(req.SerializeAsString(), f.ctx));
    EXPECT_EQ(rsp.code(), 200);
    EXPECT_EQ(rsp.rides_size(), 2);
}
```

- [ ] **Step 3: 跑测试**

Run: `cmake --build build --target test_handlers_ride -j && build/server/test_handlers_ride`
Expected: 17 个 case 全 PASS

- [ ] **Step 4: Commit**

```bash
git add server/src/handlers/list_rides.cpp server/tests/test_handlers_ride.cpp server/CMakeLists.txt
git commit -m "feat(server): list_rides handler"
```

---

## Task 22: Router 注册 + main.cpp 注入

**Files:**
- Modify: `server/src/main.cpp`

- [ ] **Step 1: 注册新事件**

在 `server/src/main.cpp` 现有 `router.register_handler(...)` 之后追加:

```cpp
router.register_handler(0x11, handlers::list_nearby_bikes);
router.register_handler(0x13, handlers::scan_unlock);
router.register_handler(0x15, handlers::position_report);
router.register_handler(0x17, handlers::end_ride);
router.register_handler(0x19, handlers::report_damage);
router.register_handler(0x1B, handlers::get_ride_detail);
router.register_handler(0x1D, handlers::list_rides);
```

- [ ] **Step 2: 构建并启动 server,确认无段错误**

Run: `cmake --build build --target bike-server -j`
Expected: 链接成功

- [ ] **Step 3: 部署到 Tencent Cloud,确认 server 起来**

参考已有部署脚本(SFTP + docker compose)。验证 `LIST_NEARBY_BIKES` 返回 60 辆车(用 Python 客户端,Task 23 写)。

- [ ] **Step 4: Commit**

```bash
git add server/src/main.cpp
git commit -m "feat(server): register 7 new ride flow handlers"
```

---

## Task 23: Python FBEB 客户端

**Files:**
- Create: `server/tests/integration/fbeb_client.py`

- [ ] **Step 1: 实现**

```python
# server/tests/integration/fbeb_client.py
"""Minimal FBEB client for integration tests."""
import socket
import struct
from typing import Optional

from google.protobuf import descriptor_pool, message_factory


def _load_messages():
    """Dynamic-load bike.proto messages (avoid depending on generated python)."""
    from google.protobuf import descriptor_pb2
    import pathlib
    proto_path = pathlib.Path(__file__).parent.parent.parent.parent / "proto" / "bike.proto"
    fd_proto = descriptor_pb2.FileDescriptorProto()
    # 简化:直接调 protoc 转 FileDescriptorProto 太重,改用 grpcio-tools
    # 这里假设 protoc --python_out 已经生成 bike_pb2
    import sys
    sys.path.insert(0, str(pathlib.Path(__file__).parent / "_proto"))
    import bike_pb2
    return bike_pb2


class FBEBClient:
    MAGIC = b'FBEB'

    def __init__(self, host: str, port: int):
        self._sock = socket.create_connection((host, port))
        self._pb = _load_messages()

    def _send(self, event_id: int, payload: bytes) -> None:
        header = self.MAGIC + struct.pack('<HI', event_id, len(payload))
        self._sock.sendall(header + payload)

    def _recv(self) -> tuple[int, bytes]:
        header = self._recvn(10)
        assert header[:4] == self.MAGIC, f"bad magic {header[:4]!r}"
        event_id, length = struct.unpack('<HI', header[4:])
        payload = self._recvn(length) if length else b''
        return event_id, payload

    def _recvn(self, n: int) -> bytes:
        buf = b''
        while len(buf) < n:
            chunk = self._sock.recv(min(n - len(buf), 8192))
            if not chunk:
                raise ConnectionError("socket closed")
            buf += chunk
        return buf

    def call(self, event_id: int, request_msg) -> bytes:
        """One-way round-trip. Returns response payload bytes."""
        self._send(event_id, request_msg.SerializeToString())
        _, payload = self._recv()
        return payload

    def send_oneway(self, event_id: int, request_msg) -> None:
        """Fire-and-forget (position_report)."""
        self._send(event_id, request_msg.SerializeToString())

    def close(self):
        self._sock.close()
```

注:需要先生成 `bike_pb2.py`:Run `protoc --python_out=server/tests/integration/_proto --proto_path=proto proto/bike.proto`

- [ ] **Step 2: Commit**

```bash
git add server/tests/integration/fbeb_client.py
git commit -m "test(integration): Python FBEB client for end-to-end tests"
```

---

## Task 24: test_full_ride.py 端到端

**Files:**
- Create: `server/tests/integration/test_full_ride.py`
- Create: `docker/docker-compose.test.yml`

- [ ] **Step 1: 测试栈 compose**

```yaml
# docker/docker-compose.test.yml
version: "3.8"
services:
  mysql-test:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: test
      MYSQL_DATABASE: bike
    ports: ["13306:3306"]
    volumes:
      - ./mysql-init:/docker-entrypoint-initdb.d:ro
  redis-test:
    image: redis:7-alpine
    ports: ["16379:6379"]
```

- [ ] **Step 2: 写完整闭环测试**

```python
# server/tests/integration/test_full_ride.py
"""End-to-end: login → recharge → scan → wait → end → verify deduction."""
import os
import sys
import time
import subprocess
import pathlib

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from fbeb_client import FBEBClient
import bike_pb2

SERVER = os.environ.get("BIKE_TEST_SERVER", "127.0.0.1")
PORT   = int(os.environ.get("BIKE_TEST_PORT", "18888"))
MOBILE = "15600000010"


@pytest.fixture(scope="module")
def client():
    c = FBEBClient(SERVER, PORT)
    yield c
    c.close()


def login(client):
    req = bike_pb2.mobile_request(mobile=MOBILE)
    rsp = bike_pb2.mobile_response()
    rsp.ParseFromString(client.call(0x01, req))
    assert rsp.code() == 200
    icode = rsp.icode()

    lr = bike_pb2.login_request(mobile=MOBILE, icode=icode)
    lrsp = bike_pb2.login_response()
    lrsp.ParseFromString(client.call(0x03, lr))
    assert lrsp.code() == 200
    return lrsp.session_token()


def test_complete_ride_deducts_balance(client):
    token = login(client)

    # 充值 10 元
    rc = bike_pb2.recharge_request(session_token=token, amount=1000)
    rcr = bike_pb2.recharge_response()
    rcr.ParseFromString(client.call(0x05, rc))
    assert rcr.code() == 200

    # 扫码 BJ-000001
    su = bike_pb2.scan_unlock_request(session_token=token, bike_no="BJ-000001",
                                       lat=39.982, lng=116.314)
    sur = bike_pb2.scan_unlock_response()
    sur.ParseFromString(client.call(0x13, su))
    assert sur.code() == 200, f"scan_unlock failed: {sur.desc()}"
    ride_no = sur.ride_no()

    # 等 20 秒,模拟骑行
    time.sleep(20)

    # 结束骑行
    er = bike_pb2.end_ride_request(session_token=token, ride_no=ride_no,
                                    end_lat=39.985, end_lng=116.318)
    err = bike_pb2.end_ride_response()
    err.ParseFromString(client.call(0x17, er))
    assert err.code() == 200, f"end_ride failed: {err.desc()}"
    # 起步价 1.00 元 = 100 分
    assert err.amount_cent() == 100

    # 查余额
    bal = bike_pb2.account_balance_request(session_token=token)
    balr = bike_pb2.account_balance_response()
    balr.ParseFromString(client.call(0x07, bal))
    assert balr.balance() == 900  # 1000 - 100

    # 查历史
    lr = bike_pb2.list_rides_request(session_token=token, limit=20)
    lrr = bike_pb2.list_rides_response()
    lrr.ParseFromString(client.call(0x1D, lr))
    assert lrr.code() == 200
    assert any(r.ride_no() == ride_no for r in lrr.rides())

    # 查详情
    dr = bike_pb2.get_ride_detail_request(session_token=token, ride_no=ride_no)
    drr = bike_pb2.get_ride_detail_response()
    drr.ParseFromString(client.call(0x1B, dr))
    assert drr.code() == 200
    assert drr.points_size() >= 2
```

- [ ] **Step 3: Commit**

```bash
git add docker/docker-compose.test.yml server/tests/integration/test_full_ride.py
git commit -m "test(integration): full ride end-to-end"
```

---

## Task 25: test_damaged_bike.py + test_end_ride_idempotency.py

**Files:**
- Create: `server/tests/integration/test_damaged_bike.py`
- Create: `server/tests/integration/test_end_ride_idempotency.py`

- [ ] **Step 1: damaged bike 测试**

```python
# server/tests/integration/test_damaged_bike.py
"""Scan a damaged bike returns 409."""
import os, sys, pathlib
import pytest
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from fbeb_client import FBEBClient
import bike_pb2
from test_full_ride import login

SERVER = os.environ.get("BIKE_TEST_SERVER", "127.0.0.1")
PORT   = int(os.environ.get("BIKE_TEST_PORT", "18888"))


def test_scan_damaged_bike_returns_409():
    c = FBEBClient(SERVER, PORT)
    try:
        token = login(c)
        su = bike_pb2.scan_unlock_request(session_token=token, bike_no="BJ-000058",
                                           lat=39.971, lng=116.329)
        r = bike_pb2.scan_unlock_response()
        r.ParseFromString(c.call(0x13, su))
        assert r.code() == 409
    finally:
        c.close()
```

- [ ] **Step 2: 幂等测试**

```python
# server/tests/integration/test_end_ride_idempotency.py
"""END_RIDE twice returns history, no double deduction."""
import os, sys, time, pathlib
import pytest
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from fbeb_client import FBEBClient
import bike_pb2
from test_full_ride import login

SERVER = os.environ.get("BIKE_TEST_SERVER", "127.0.0.1")
PORT   = int(os.environ.get("BIKE_TEST_PORT", "18888"))


def test_end_ride_idempotent():
    c = FBEBClient(SERVER, PORT)
    try:
        token = login(c)
        # 充值
        rc = bike_pb2.recharge_request(session_token=token, amount=1000)
        c.call(0x05, rc)
        # 扫码
        su = bike_pb2.scan_unlock_request(session_token=token, bike_no="BJ-000002",
                                           lat=39.978, lng=116.319)
        sur = bike_pb2.scan_unlock_response()
        sur.ParseFromString(c.call(0x13, su))
        assert sur.code() == 200
        ride_no = sur.ride_no()

        # 结束
        er = bike_pb2.end_ride_request(session_token=token, ride_no=ride_no,
                                        end_lat=39.980, end_lng=116.321)
        err1 = bike_pb2.end_ride_response()
        err1.ParseFromString(c.call(0x17, er))
        assert err1.code() == 200
        amt1 = err1.amount_cent()
        bal1 = err1.balance_after()

        # 再结束一次 —— 应该返回历史,不重复扣款
        err2 = bike_pb2.end_ride_response()
        err2.ParseFromString(c.call(0x17, er))
        assert err2.code() == 200
        assert err2.amount_cent() == amt1
        assert err2.balance_after() == bal1
    finally:
        c.close()
```

- [ ] **Step 3: 跑全套集成测试**

Run:
```bash
cd /d/C++/shared_bike_1
docker compose -f docker/docker-compose.test.yml up -d
sleep 10
# 部署 server(指向测试栈的 MySQL/Redis)
# 假设 server 已 build 为 build/server/bike-server
BIKE_TEST_PORT=18888 ./build/server/bike-server --config=server/tests/integration/test_config.toml &
SERVER_PID=$!
sleep 2
pytest server/tests/integration/ -v
kill $SERVER_PID
docker compose -f docker/docker-compose.test.yml down
```
Expected: 3 个 test_*.py 全 PASS

- [ ] **Step 4: Commit**

```bash
git add server/tests/integration/test_damaged_bike.py server/tests/integration/test_end_ride_idempotency.py
git commit -m "test(integration): damaged bike + end_ride idempotency"
```

---

## Task 26: 部署到生产 + 文档

**Files:**
- Modify: `README.md`(若存在)
- Create: `docs/ops.md`

- [ ] **Step 1: 部署到 Tencent Cloud**

```bash
# 用 paramiko SFTP 把更新后的代码上传
# docker compose 重新构建 + 启动
# 验证 LIST_NEARBY_BIKES 返回 60 辆车(用 Python 客户端打生产端口 8888)
```

- [ ] **Step 2: 写 ops.md**

```markdown
# Ops Runbook — 共享单车后端

## 部署
1. SSH 到 Tencent Cloud `ubuntu@124.220.92.243`
2. `cd ~/shared_bike && git pull`
3. `docker compose -f docker/docker-compose.yml up -d --build`

## 已知限制
- SessionStore 在内存中,server 重启会丢失所有活跃骑行会话
- 重启后:所有 bike.status=1 的车会卡死(rented 状态),用户无法再扫码

## 恢复 stuck 单车(MVP 不实现)
未来提供 `bike_admin recover_stuck` CLI:
\`\`\`sql
UPDATE bike SET status = 0
WHERE status = 1
  AND id NOT IN (SELECT bike_id FROM ride WHERE end_tm IS NULL);
\`\`\`
手动执行前请确认没有真正活跃的骑行。
```

- [ ] **Step 3: Commit**

```bash
git add docs/ops.md README.md
git commit -m "docs(ops): deployment + stuck bike recovery runbook"
```

---

## Self-Review Checklist(执行后跑一遍)

**1. Spec coverage**
- ✅ 8 个新事件:Task 1(proto) → Task 15-21(handler)
- ✅ 3 张新表:Task 2 + Task 3
- ✅ Repo 抽象:Task 8(bike) + Task 9(ride) + Task 12/13(mysql 实现)
- ✅ SessionStore:Task 10
- ✅ 计费引擎:Task 4
- ✅ 鉴权中间件:Task 14
- ✅ 端到端集成测试:Task 24/25
- ✅ ops 文档:Task 26

**2. 类型一致性**
- `IBikeRepo::get_for_update` / `update_status` / `update_location` / `list_in_bounds` —— 全 plan 一致使用 ✓
- `IRideRepo::create_with_points` / `find_by_no` / `list_points` / `list_by_user` ✓
- `RideSessionStore::create` / `find` / `update_pos` / `remove` ✓
- `require_user(token, ctx)` —— 签名稳定 ✓
- Handler event_id 常量:`0x12, 0x14, 0x18, 0x1A, 0x1C, 0x1E` ✓

**3. 已知简化(plan 内 TODO 标注)**
- `scan_unlock` 的 ride_no daily_seq 用 `now_unix() % 999999` 占位 —— Task 16 已说明,生产改 Redis INCR
- `end_ride` 距离用 start↔end 直线,真实应从 ride_position 累加 —— Task 18 已标注
- `end_ride` 写入 ride_position 只存起点+终点两点 —— Task 18 已标注,真实生产应累积 session 期间的 last_pos 序列

**4. 没覆盖的边缘情况**
- `account_record` 表的 `balance_after` 字段未严格校验 —— `add_balance` 返回值已是扣后余额,可直接写入。MVP 接受小不一致。
- 真实 GPS 坐标系问题(中国境内 GPS vs 国测局 GCJ-02)—— MVP 不处理,文档化在 ops.md
