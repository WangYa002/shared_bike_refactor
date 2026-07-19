# 共享单车扩展设计 — 骑行闭环 + 真实地图

> **状态**:设计草案,待评审
> **日期**:2026-07-19
> **范围**:在现有"登录 / 钱包 / 账单"基础上,扩展出完整的"扫码解锁 → 骑行 → 自动扣费 → 轨迹回放"闭环

## 1. 目标与非目标

### 目标
1. 用户能在地图上看到周围的共享单车
2. 扫码(或选最近车)解锁,服务端创建订单,扣款延迟到结束时计算
3. 骑行期间客户端每秒模拟一个轨迹点,上报服务端,服务端在内存中维护活跃会话
4. 用户主动结束骑行 → 服务端按分钟计费 → 余额扣款 → 订单归档
5. 历史订单列表 + 单次订单详情(含完整轨迹回放)
6. 用户可上报故障车,故障车在地图上标红且扫码会失败

### 非目标(YAGNI)
- 多人共享同一张地图(方案 B 的广播机制)—— 本设计退化为"只显示自己"
- 真实 GPS 信号 —— 笔记本无 GPS,起点用 WinRT 定位,行程用本地模拟
- 真实扫码摄像头 —— 选最近 idle 车触发解锁
- 服务端 SessionStore 持久化 —— 崩溃丢失可接受(README 文档化)
- 套餐 / 月卡 / 高峰定价 / 多城市差异 —— 单一计费规则
- 第三方支付通道 —— 走现有账户余额
- 真并发压测 / UI 自动化测试

## 2. 现状回顾

### 现有事件(0x01–0x10,不动)
| ID | 名称 |
|---|---|
| 0x01 / 0x02 | `mobile_code` |
| 0x03 / 0x04 | `login` |
| 0x05 / 0x06 | `recharge` |
| 0x07 / 0x08 | `account_balance` |
| 0x0F / 0x10 | `list_account_records` |

### 现有数据库表(不动)
- `userinfo(id, mobile, username, registertm)`
- `account(user_id, balance)` —— 余额单位:分
- `account_record(id, user_id, type, amount, balance_after, tm)` —— type=1 充值 / type=2 消费

### 现有客户端
- Qt6 + MSVC + vcpkg,FBEB 协议
- 三 tab:登录 / 钱包 / 账单
- 单连接 BackendClient,所有请求走 `round_trip` 同步等待

## 3. 架构

### 3.1 编号约定
请求 ID 为奇数,响应 ID = 请求 ID + 1。单向事件仅占用请求 ID,响应 ID 不分配。

### 3.2 新增 8 个事件
| ID | 名称 | 方向 |
|---|---|---|
| 0x11 / 0x12 | `list_nearby_bikes` | 请求/响应 |
| 0x13 / 0x14 | `scan_unlock` | 请求/响应 |
| 0x15 | `ride_position_report` | 单向(C→S,无响应) |
| 0x17 / 0x18 | `end_ride` | 请求/响应 |
| 0x19 / 0x1A | `report_damage` | 请求/响应 |
| 0x1B / 0x1C | `get_ride_detail` | 请求/响应 |
| 0x1D / 0x1E | `list_rides` | 请求/响应 |

(0x16 保留给未来"服务端推送位置更新",本设计未启用)

### 3.3 服务端新增组件

```
server/
  src/
    ride/
      session_store.hpp      内存表:ride_no → RideSession
      session_store.cpp
      pricing.hpp            计费引擎(纯函数)
      pricing.cpp
    handlers/
      list_nearby_bikes.cpp
      scan_unlock.cpp
      position_report.cpp    仅更新 session_store,不发响应
      end_ride.cpp           计费 + 写 DB + 清 session
      report_damage.cpp
      get_ride_detail.cpp
      list_rides.cpp
    repo/
      bike_repo.hpp          抽象 IBikeRepo(便于测试)
      bike_repo.cpp          MySQL 实现
      ride_repo.hpp / .cpp
      user_repo.hpp / .cpp
  include/server/
    auth.hpp                 require_user(token) → optional<user_id>
    session_store.hpp
    pricing.hpp
```

### 3.4 客户端新增组件

```
client/
  src/
    views/
      map_view.cpp           QWebEngineView + Leaflet + AMap 瓦片
      ride_view.cpp          骑行中面板(计时、轨迹、结束按钮)
      ride_history_view.cpp  历史列表
      ride_detail_dialog.cpp 轨迹回放对话框
    trajectory_sim.hpp / .cpp  本地轨迹生成器
    location_provider.hpp / .cpp  WinRT Geolocation 抽象
  resources/
    map.qrc                  打包 map.html / map.js / map.css
    map.html
    map.js                   Leaflet + QWebChannel 通道
  include/client/
    backend_client.hpp       新增 7 个方法
```

### 3.5 数据流(单次完整骑行)

```
登录 → 进地图 → LIST_NEARBY_BIKES(地图显示周围车)
     → 选车 → SCAN_UNLOCK
        ← 服务端:RideSession 创建,bike status idle→rented,返回 ride_no + start_ts
     → 跳到 ride_view → 启动 1Hz QTimer
        每秒:① trajectory_sim.step() 产生新点
              ② BackendClient::report_position 异步发送(独立 socket)
              ③ 通知 JS appendTrajectory + 更新计时/距离
     → 点"结束骑行" → END_RIDE
        ← 服务端:计算费用 + 扣余额 + INSERT ride + INSERT ride_position + bike status rented→idle + 删 session
     → 跳到账单 tab,显示消费 toast
     → (可选)历史 → 选这笔 → GET_RIDE_DETAIL → 弹 RideDetailDialog 回放轨迹
```

## 4. 数据模型

### 4.1 新增 3 张表

```sql
-- 单车资产表
CREATE TABLE IF NOT EXISTS bike (
  id         int          NOT NULL PRIMARY KEY AUTO_INCREMENT,
  bike_no    varchar(32)  NOT NULL UNIQUE,
  lat        decimal(10,7) NOT NULL,
  lng        decimal(10,7) NOT NULL,
  status     tinyint      NOT NULL DEFAULT 0,    -- 0=idle 1=rented 2=damaged
  created_at timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- 骑行订单(结束后归档)
CREATE TABLE IF NOT EXISTS ride (
  id            bigint       NOT NULL PRIMARY KEY AUTO_INCREMENT,
  ride_no       varchar(32)  NOT NULL UNIQUE,    -- R + yyyyMMdd + 6 位序号
  user_id       int          NOT NULL,
  bike_id       int          NOT NULL,
  start_tm      timestamp    NOT NULL,
  end_tm        timestamp    NOT NULL,
  start_lat     decimal(10,7) NOT NULL,
  start_lng     decimal(10,7) NOT NULL,
  end_lat       decimal(10,7) NOT NULL,
  end_lng       decimal(10,7) NOT NULL,
  duration_sec  int          NOT NULL,           -- 冗余字段,加速历史查询
  distance_m    int          NOT NULL,
  amount_cent   int          NOT NULL,
  status        tinyint      NOT NULL DEFAULT 0, -- 0=completed 1=auto_cancelled
  INDEX idx_user_start (user_id, start_tm),
  INDEX idx_bike_start (bike_id, start_tm),
  FOREIGN KEY (user_id) REFERENCES userinfo(id),
  FOREIGN KEY (bike_id) REFERENCES bike(id)
);

-- 轨迹点(每秒一个点)
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

### 4.2 设计要点
- **坐标精度**:`DECIMAL(10,7)` ≈ 0.01 米精度,比 `FLOAT` 安全(无累加误差),比 `INT * 1e7` 易读
- **轨迹独立一张表**:平均 15 分钟 × 1Hz = 900 行/单;`ride` 表冗余 `distance_m / duration_sec`,历史列表不碰 `ride_position`
- **无 fare_rule 表**:单一计费规则硬编码在 `pricing.cpp`,YAGNI

### 4.4 计费规则(明确)

**起步价 1.00 元,包含 15 分钟;之后每 15 分钟 0.50 元,不足一档按一档向上取整。**

| 骑行时长 | 费用 |
|---|---|
| 0–15:00(含) | ¥ 1.00 |
| 15:01–30:00 | ¥ 1.50 |
| 30:01–45:00 | ¥ 2.00 |
| 45:01–60:00 | ¥ 2.50 |
| 60:01–75:00 | ¥ 3.00 |
| (每 15 分钟 + 0.50) | … |

无封顶,无夜间优惠,无起步免费时长。

实现(消除边界歧义):
```cpp
int compute_fee(int duration_sec) {
    if (duration_sec < 0) throw std::invalid_argument{"negative duration"};
    constexpr int base_sec = 15 * 60;
    constexpr int step_sec = 15 * 60;
    int fee = 100;                                          // 起步价(分)
    if (duration_sec <= base_sec) return fee;
    int extra_sec = duration_sec - base_sec;
    int extra_chunks = (extra_sec + step_sec - 1) / step_sec;  // ceil
    return fee + extra_chunks * 50;
}
```

边界点 15:00 整秒、30:00 整秒等仍属于上一档(因为 `duration_sec <= base_sec`)。

### 4.3 Seed 脚本(`docker/mysql-init/seed_bikes.sql`,字母序在 schema.sql 之后)

```sql
-- 60 辆车随机分布在五道口附近(39.96–39.99 N, 116.31–116.34 E)
-- 其中 3 辆设为 damaged,演示扫码失败场景
INSERT INTO bike (bike_no, lat, lng, status) VALUES
  ('BJ-000001', 39.9821000, 116.3145000, 0),
  ('BJ-000002', 39.9783000, 116.3198000, 0),
  -- ... 共 60 行
  ('BJ-000058', 39.9712000, 116.3287000, 2),    -- damaged
  ('BJ-000059', 39.9845000, 116.3162000, 2),    -- damaged
  ('BJ-000060', 39.9768000, 116.3211000, 2);    -- damaged
;
```

## 5. 事件 / API 详细规范

### 5.1 Protobuf 消息定义(追加到 `proto/bike.proto`)

```proto
// ---------- 0x11 / 0x12 — 周围空闲车辆 ----------
message list_nearby_bikes_request {
  string session_token = 1;
  double lat = 2;
  double lng = 3;
  double radius_m = 4;     // 默认 500,上限 2000
}
message bike_info {
  string bike_no = 1;
  double lat      = 2;
  double lng      = 3;
  int32  status   = 4;     // 0=idle 1=rented 2=damaged
}
message list_nearby_bikes_response {
  int32 code = 1;
  repeated bike_info bikes = 2;
}

// ---------- 0x13 / 0x14 — 扫码解锁 ----------
message scan_unlock_request {
  string session_token = 1;
  string bike_no       = 2;
  double lat = 3;
  double lng = 4;
}
message scan_unlock_response {
  int32  code     = 1;
  string desc     = 2;
  string ride_no  = 3;
  int64  start_ts = 4;
}

// ---------- 0x15 — 位置上报(单向) ----------
message ride_position_report {
  string ride_no   = 1;
  int32  seq       = 2;
  double lat       = 3;
  double lng       = 4;
  int32  elapsed_sec = 5;
}

// ---------- 0x17 / 0x18 — 结束骑行 ----------
message end_ride_request {
  string session_token = 1;
  string ride_no       = 2;
  double end_lat = 3;
  double end_lng = 4;
}
message end_ride_response {
  int32  code          = 1;
  string desc          = 2;
  int32  duration_sec  = 3;
  int32  distance_m    = 4;
  int32  amount_cent   = 5;
  int32  balance_after = 6;
}

// ---------- 0x19 / 0x1A — 报修坏车 ----------
message report_damage_request {
  string session_token = 1;
  string bike_no       = 2;
  string note          = 3;
}
message report_damage_response {
  int32  code = 1;
  string desc = 2;
}

// ---------- 0x1B / 0x1C — 单次骑行详情 ----------
message get_ride_detail_request {
  string session_token = 1;
  string ride_no       = 2;
}
message ride_point {
  double lat = 1;
  double lng = 2;
  int32  elapsed_sec = 3;
}
message get_ride_detail_response {
  int32  code          = 1;
  string ride_no       = 2;
  int32  duration_sec  = 3;
  int32  distance_m    = 4;
  int32  amount_cent   = 5;
  string start_tm      = 6;
  string end_tm        = 7;
  repeated ride_point points = 8;
}

// ---------- 0x1D / 0x1E — 历史骑行列表 ----------
message list_rides_request {
  string session_token = 1;
  int32  limit = 2;       // 默认 20,上限 100
}
message ride_summary {
  string ride_no      = 1;
  string start_tm     = 2;
  int32  duration_sec = 3;
  int32  distance_m   = 4;
  int32  amount_cent  = 5;
}
message list_rides_response {
  int32 code = 1;
  repeated ride_summary rides = 2;
}
```

### 5.2 服务端关键行为

**`scan_unlock` (0x13 / 0x14)**
1. `require_user(token)` → user_id;失败返回 `401`
2. 输入校验:`bike_no` 长度 1–32,lat ∈ [-90, 90],lng ∈ [-180, 180]
3. 开启事务:`SELECT * FROM bike WHERE bike_no=? FOR UPDATE`(行锁)
4. 检查 status:`damaged` 返回 `409`,`rented` 返回 `408`,`idle` 继续
5. 余额检查:`SELECT balance FROM account WHERE user_id=?`,若 < 100 返回 `406 desc="余额不足"`
6. 生成 ride_no:`R + yyyyMMdd + 6 位 daily-seq`(daily-seq 从 `ride_daily_seq` Redis key 取,INCR + TTL 到当天结束)
7. 写 SessionStore:`{ride_no, user_id, bike_id, start_lat, start_lng, start_ts, last_pos, last_seq=0}`
8. `UPDATE bike SET status=1 WHERE id=?`
9. 提交事务,返回 `ride_no + start_ts`

**`ride_position_report` (0x15)**
1. 查 SessionStore,无 DB 访问
2. 校验 session.user_id == token 解析出的 user_id
3. 更新 last_pos / last_seq
4. 直接 return,不发响应

**`end_ride` (0x17 / 0x18)** — 详见第 7 节幂等设计
1. `require_user(token)`
2. SessionStore.find(ride_no):
   - 命中 → 正常流程
   - 未命中 → 查 ride 表(幂等):若存在且 user_id 匹配 → 返回历史结果;若不存在 → 404;若 user_id 不匹配 → 401
3. 正常流程:pricing 计算 amount
4. 事务:
   - `UPDATE account SET balance = balance - amount WHERE user_id=? AND balance >= amount`(影响行数 == 0 → 余额不足,回滚,返回 406)
   - `INSERT INTO account_record(user_id, type=2, amount, balance_after)`
   - `INSERT INTO ride(...)`,拿 ride.id
   - 批量 `INSERT INTO ride_position(...)`
   - `UPDATE bike SET status=0, lat=?, lng=? WHERE id=?`
   - SessionStore.remove(ride_no)
5. 返回计费明细

**`list_nearby_bikes` (0x11 / 0x12)**
1. `require_user(token)`
2. clamp radius_m 到 [50, 2000]
3. 用 lat/lng ± radius_m 算出 bounding box,`SELECT * FROM bike WHERE status IN (0,2) AND lat BETWEEN ? AND ? AND lng BETWEEN ? AND ?`
4. 应用层用 Haversine 精过滤
5. damaged 车也返回(客户端标红)

**`report_damage` (0x19 / 0x1A)**
1. `require_user(token)`
2. `SELECT * FROM bike WHERE bike_no=?`
3. 不存在 → 404
4. `UPDATE bike SET status=2 WHERE id=?`(若已经是 damaged,无副作用)
5. 返回 200

**`get_ride_detail` (0x1B / 0x1C)**
1. `require_user(token)`
2. `SELECT * FROM ride WHERE ride_no=?`
3. 不存在 → 404;user_id 不匹配 → 401
4. `SELECT * FROM ride_position WHERE ride_id=? ORDER BY seq`
5. 拼装响应

**`list_rides` (0x1D / 0x1E)**
1. `require_user(token)`
2. clamp limit 到 [1, 100]
3. `SELECT * FROM ride WHERE user_id=? ORDER BY start_tm DESC LIMIT ?`

### 5.3 错误码映射(沿用 `errors.hpp`,不新增)

| 场景 | code | desc(中文) |
|---|---|---|
| session 无效 | 401 | "登录已过期,请重新登录" |
| bike_no 不存在 | 404 | "车辆不存在" |
| ride_no 不存在 | 404 | "订单不存在" |
| ride_no 不属于当前用户 | 401 | "无权访问该订单" |
| 余额不足 | 406 | "余额不足" |
| 服务端异常 | 406 | "服务端错误" |
| bike 已被租用 | 408 | "车辆正在使用中" |
| bike 报修中 | 409 | "车辆故障,请选择其他车辆" |

## 6. 客户端 UX

### 6.1 主窗口结构(改造)

`QStackedWidget` 包含两页:
- **登录前**:LoginView(占满)
- **登录后**:MainContainer(QTabWidget,5 个 tab)

| tab | 默认状态 | 触发条件 |
|---|---|---|
| 地图 (MapView) | 启用 | 登录后默认显示 |
| 骑行中 (RideView) | 禁用 | SCAN_UNLOCK 成功 → 启用并跳转;END_RIDE 成功 → 禁用 |
| 钱包 (WalletView) | 启用 | (沿用) |
| 账单 (RecordsView) | 启用 | (沿用) |
| 历史 (RideHistoryView) | 启用 | 新增 |

### 6.2 MapView

```
┌──────────────────────────────────────────┐
│  [QWebEngineView - Leaflet 地图]          │
│      🚲           🚲                       │
│         🚲  🛠(红)     🚲                 │
│                       📍(我)              │
│  ┌─────────────────────────┐  [📍] [🔄]   │
│  │ 附近:5 可用 / 1 故障    │              │
│  └─────────────────────────┘              │
│  ┌─────────────────────────┐              │
│  │     [   扫码解锁   ]    │              │
│  └─────────────────────────┘              │
└──────────────────────────────────────────┘
```

- AMap 瓦片:`https://webrd0{1-4}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}`
- 蓝色圆点 = idle,tap 显示 bike_no + 解锁按钮
- 红色扳手 = damaged,tap 显示"故障车,已上报"
- "扫码解锁"按钮:MVP 无摄像头,直接选最近 idle 车触发 SCAN_UNLOCK
- 📍 定位:WinRT `Windows.Devices.Geolocation`,失败回退到默认(五道口中心)
- 🔄 刷新:重新拉 LIST_NEARBY_BIKES

### 6.3 RideView

```
┌──────────────────────────────────────────┐
│ 骑行中  R20260719000001                    │
│                                           │
│   ⏱  00:03:42                             │
│   ─────────────                           │
│   📏 0.46 km    💰 约 ¥ 1.50               │
│                                           │
│   [小地图 - 实时轨迹]                      │
│                                           │
│   ┌─────────────────────────┐             │
│   │    结束骑行              │             │
│   └─────────────────────────┘             │
└──────────────────────────────────────────┘
```

- 进入此页启动 1Hz QTimer → `trajectory_sim.step()` → 产生新点
- 每个新点触发三件事:
  1. `BackendClient::report_position`(异步,独立 socket)
  2. JS 通道:`appendTrajectory(lat, lng)`
  3. 更新 UI:计时器、距离、估算费用
- "结束骑行"红色大按钮 → `BackendClient::end_ride` → 成功后跳到账单 tab,弹 toast"消费 ¥ X.XX"

### 6.4 RideHistoryView

```
┌──────────────────────────────────────────┐
│ 历史骑行                                   │
│  ┌─────────────────────────────────────┐ │
│  │ R20260718000012                     │ │
│  │ 07-18 14:32   1.2 km   12 min  ¥2.50│ │
│  │                          [查看轨迹]  │ │
│  └─────────────────────────────────────┘ │
│  ...                                      │
│  [🔄 刷新]                                 │
└──────────────────────────────────────────┘
```

`QTableWidget` 4 列:订单号 / 起始时间 / 距离+时长 / 金额+按钮。

### 6.5 RideDetailDialog

```
┌──────────────────────────────────────────┐
│ 订单 R20260718000012               [ × ]  │
├──────────────────────────────────────────┤
│ 2026-07-18 14:32:11 → 14:44:35            │
│ 时长 12:24   距离 1.20 km   费用 ¥ 2.50    │
├──────────────────────────────────────────┤
│  [Leaflet 地图 - 完整轨迹 polyline]       │
│     ●━━━━━━━━━━━━━━━━━━━━━━━●             │
├──────────────────────────────────────────┤
│ [◀ 回放]  00:00 / 12:24   [▶ 暂停]        │
└──────────────────────────────────────────┘
```

- 进入时调 GET_RIDE_DETAIL 拿全部 points
- 一次画完整 polyline,小圆点跟随回放进度
- QTimer 按 elapsed_sec 1× 推进,可暂停

### 6.6 BackendClient 改造

新增 7 个方法:
```cpp
ListNearbyBikesResponse  list_nearby_bikes(const std::string& token, double lat, double lng, double radius_m);
ScanUnlockResponse       scan_unlock(const std::string& token, const std::string& bike_no, double lat, double lng);
void                     report_position(const std::string& ride_no, int seq, double lat, double lng, int elapsed_sec);  // 单向
EndRideResponse          end_ride(const std::string& token, const std::string& ride_no, double lat, double lng);
ReportDamageResponse     report_damage(const std::string& token, const std::string& bike_no, const std::string& note);
GetRideDetailResponse    get_ride_detail(const std::string& token, const std::string& ride_no);
ListRidesResponse        list_rides(const std::string& token, int limit);
```

`report_position` 不调 `round_trip`(不读响应),走独立 `pos_socket_`,带 backpressure(上一秒未发完则丢弃本次)。

### 6.7 TrajectorySim

```cpp
class TrajectorySim : public QObject {
    Q_OBJECT
public:
    explicit TrajectorySim(uint32_t seed);
    void start(double lat0, double lng0);
    void step();                    // 每秒一次
    QPointF current() const;
    double distance_m() const;
signals:
    void moved(double lat, double lng);
};
```

- 速度模型:8–15 km/h(典型共享单车),用 `std::mt19937` 抽样
- 方向模型:每秒抖动 ±15°,保持大致直线
- 累加用 Haversine 算 distance_m

## 7. 错误处理 + 并发

### 7.1 服务端并发
- MySQL 连接池大小 8,与 asio 工作线程数一致
- SessionStore 用 `std::shared_mutex` 保护 `unordered_map<ride_no, RideSession>`:位置上报读用 shared_lock,scan_unlock/end_ride 写用 unique_lock
- 同一 bike 并发扫码:`SELECT FOR UPDATE` 行锁串行化
- 余额扣减并发:`UPDATE account SET balance=balance-? WHERE user_id=? AND balance>=?`,检查影响行数

### 7.2 客户端并发
- BackendClient 主连接仍是单 socket,所有 `round_trip` 在 QtConcurrent 工作线程串行
- 位置上报独立 socket,`pos_sending_` 原子 flag 防止堆积
- 上一秒未发完则直接丢弃本次,不进入队列

### 7.3 END_RIDE 幂等

服务端流程(伪码):
```cpp
auto ride_opt = sessions.find(ride_no);
if (ride_opt) {
    return do_end_ride(*ride_opt, end_lat, end_lng);   // 正常扣款 + 写库
}
// SessionStore 未命中,查历史(可能是客户端重试)
auto row = ride_repo.find_by_no(ride_no);
if (!row)            return error(404);
if (row.user_id != uid) return error(401);
return make_response_from_history(*row);                // 同 end_ride_response 格式
```

### 7.4 网络异常分类

| 场景 | 客户端行为 | 服务端行为 |
|---|---|---|
| `report_position` 丢包 | 静默,下秒覆盖 | — |
| `round_trip` 超时(5s) | 抛 runtime_error,UI 显示"网络异常" | — |
| END_RIDE 响应丢失 | 用户重新点击"结束骑行"按钮(无自动重试),`round_trip` 走幂等路径,服务端识别已结订单 → 返回历史结果,不重复扣款 |
| 服务端崩溃后重启 | 下次 round_trip 抛 EOF,弹"重新登录" | SessionStore 全空,active rides stuck |
| TCP RST | 同上 | — |

### 7.5 服务端崩溃恢复(已知限制)

- **不实现** SessionStore 持久化
- **缓解**(不在 MVP 实现):文档化 `docs/ops.md`,提供 admin CLI `bike_admin recover_stuck` 扫描 stuck 状态(超过 1 小时仍 rented 但无对应 session),由 ops 手动执行

### 7.6 客户端崩溃恢复

- SCAN_UNLOCK 成功 → `QSettings::setValue("active_ride", ride_no)`
- END_RIDE 成功 → `QSettings::remove("active_ride")`
- 启动时检查:
  - 有 `active_ride` → 弹"检测到未结订单 Rxxx,是否恢复?"
  - "恢复" → 发 END_RIDE → 服务端 404(SessionStore 已丢)→ 清 QSettings,提示"订单已失效"
  - "放弃" → 清 QSettings

### 7.7 输入校验

服务端 handler 入口处统一校验,不能依赖客户端:
- `bike_no` 长度 1–32
- lat ∈ [-90, 90],lng ∈ [-180, 180]
- radius_m clamp 到 [50, 2000]
- limit clamp 到 [1, 100]

### 7.8 鉴权中间件

抽出 `require_user(token) -> optional<int>`:
- 复用现有 Redis,key=`session:<token>`,value=`user_id`,TTL=7 天
- login handler SET,后续 handler GET
- 每个 handler 入口:`auto uid = require_user(req.session_token()); if (!uid) return error_frame(..., 401);`

## 8. 测试策略

### 8.1 测试金字塔

```
        ┌────────────────┐
        │ 手动 UI smoke  │  1 个清单,每次发版前
        └────────────────┘
       ┌──────────────────┐
       │ Python 端到端集成 │  ~4 个 (Docker compose 起独立栈)
       └──────────────────┘
     ┌──────────────────────┐
     │ Handler 单元(Mock)  │  ~15 个 (依赖 IBikeRepo 等 fake)
     └──────────────────────┘
   ┌──────────────────────────┐
   │ 纯函数单元                │  ~25 个 (pricing, geo, ride_no, validation)
   └──────────────────────────┘
```

### 8.2 纯函数单元(GTest,无外部依赖)

- **`pricing.test.cpp`** —— 计费引擎 9 个 case(0、刚好 15:00、15:01、刚好 30:00、30:01、刚好 45:00、刚好 60:00、零碎秒向上取整、负输入)
- **`geo.test.cpp`** —— Haversine 4 个 case(零距离、已知距离、中国境内判断、纽约拒绝)
- **`ride_no.test.cpp`** —— 订单号 3 个 case(格式、序号、溢出)
- **`validation.test.cpp`** —— 输入校验 4 个 case(bike_no 格式、lat 边界、radius clamp)

### 8.3 Handler 单元(依赖 Repo 接口)

新增 `IBikeRepo / IRideRepo / IUserRepo` 抽象,handler 只依赖接口。测试注入 `FakeBikeRepo`(in-memory map)。

关键 case:
- `scan_unlock` 成功:bike status 改 + session 创建 + 返回 200
- `scan_unlock` damaged 车:返回 409 + status 不变
- `scan_unlock` rented 车:返回 408 + status 不变
- `scan_unlock` 余额不足:返回 406 + status 不变 + session 不创建
- `end_ride` 正常流程:扣款 + 写 ride + 写 position + bike 改 idle
- `end_ride` 幂等:第二次同 ride_no 返回历史结果,不重复扣款
- `end_ride` 跨用户访问:返回 401
- `list_nearby_bikes` radius clamp + bounding box 精过滤
- `report_damage` 不存在的 bike_no:404

### 8.4 SessionStore 并发

- 100 线程并发 `update_pos`:不崩溃,last_seq 单调
- `update_pos` 与 `remove` 同时跑:不死锁,无 use-after-free

### 8.5 TrajectorySim

- 60 步后 distance_m ∈ [30, 300](8–15 km/h × 60s 物理范围)
- 同 seed 同起点 → 完全相同轨迹(可重放)

### 8.6 Python 端到端集成

`server/tests/integration/test_full_ride.py` 等。docker compose 起临时栈(独立端口 18888),Python FBEB 客户端打协议:
- 完整闭环:登录 → 充值 → 扫码 → 20s 后结束 → 验证扣款 + 轨迹点数量
- damaged 车扫码失败:扫码 BJ-000058 返回 409
- 幂等:连续两次 END_RIDE,第二次返回历史结果,余额只扣一次

### 8.7 不测什么(明确)
- Qt UI 自动化 —— 人工 smoke 覆盖
- Leaflet 渲染正确性 —— 靠人工
- 真实 GPS —— 笔记本无 GPS
- 高并发压测 —— MVP 阶段 60 辆车 / 100 用户

### 8.8 人工 smoke 清单(`docs/smoke_checklist.md`)

- [ ] 客户端登录 → 钱包余额正确
- [ ] 充值 ¥ 5 → 余额更新 + 账单多一条充值
- [ ] 地图显示 ≥ 5 辆车
- [ ] 扫码解锁 → 骑行 tab 启用 → 计时开始 → 地图轨迹画出
- [ ] 1 分钟后结束骑行 → 余额扣款 → 账单多一条消费
- [ ] 历史骑行列表 → 点查看轨迹 → 弹窗地图显示完整轨迹 + 回放可暂停
- [ ] 报修一辆车 → 该车在地图变红 → 再次扫码返回 409
- [ ] 关掉客户端 → 重新打开 → 自动恢复或提示无未结订单

## 9. 实施阶段(给后续 plan 用)

1. **基础设施**:数据库迁移、Seed 脚本、proto 扩展、`errors.hpp` 复核
2. **服务端核心**:Repo 抽象、SessionStore、pricing、auth 中间件
3. **服务端事件**:7 个 handler 实现单测先,然后集成
4. **客户端基础设施**:trajectory_sim、location_provider、BackendClient 扩展、QWebChannel 桥
5. **客户端 UI**:MapView、RideView、RideHistoryView、RideDetailDialog
6. **集成**:端到端 smoke + Python 自动化
7. **文档**:README、smoke_checklist、ops runbook

## 10. 开放问题

无(所有澄清问题已在 brainstorming 阶段回答)。
