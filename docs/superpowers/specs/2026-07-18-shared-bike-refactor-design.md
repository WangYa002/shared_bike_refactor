# shared_bike_refactor — 设计文档（PRD）

- **状态**：已确认（用户授权自主执行）
- **日期**：2026-07-18
- **目标仓库**：`git@github.com:wangya002/shared_bike_refactor.git`
- **部署目标**：腾讯云 `ubuntu@124.220.92.243`（docker compose）

---

## 1. 背景

现有项目 `D:\C++\shared_bike_1` 是一个 C++03 风格的共享单车后端，依赖 libevent + protobuf + mysql + iniparser + log4cpp + 自写 pthread 线程池。仅实现了 mobile_code 与 login 两个事件，且：

- C 风格 `ConnectSession` POD + `memset` + 大量裸 `new/delete`
- `iEevent` typo 传染全项目；`void*` 在事件/session 间传引用
- `UserService::exist/insert` 用 `sprintf` 拼 SQL（注入风险）
- 每个 login 事件新建一个 `MysqlConnection`（无连接池）
- `using namespace std` 写在头文件
- 0 测试 / 0 graceful shutdown / 0 异常处理

详细架构总结见本文件附录 A。

## 2. 目标

| 目标 | 验收 |
|---|---|
| 用 C++17 重写后端 | 单二进制 `bike-server`，asio + protobuf + hiredis + mysql + spdlog + toml++ |
| 实现完整"用户 + 钱包"闭环 | mobile_code / login / recharge / account_balance / list_account_records 五个事件 |
| 引入 Redis 会话层 | 验证码、session_token 存 Redis，带 TTL |
| Docker compose 部署 | `docker compose up` 在腾讯云上拉起 server + mysql + redis 三容器 |
| 提供 Qt6 桌面客户端 | 单 GUI，能完整跑通：获取码 → 登录 → 充值 → 查余额 → 查流水 |
| 推送到 GitHub | 完整 commit 历史推到 `wangya002/shared_bike_refactor` |

## 3. 非目标（YAGNI）

- 共享单车的列表/解锁/还车/行程（被砍）
- 行程轨迹、计费规则
- 多角色（管理员后台、运营）
- 监控告警 / tracing / metrics（scope 外）
- 压测 / 集群 / 高可用

## 4. 架构

### 4.1 顶层

```
┌───────────────────────────────────────────────────────┐
│  Qt 客户端（Windows）                                  │
│  asio 同步 IO + protobuf + FBEB 帧                    │
└───────────────────┬───────────────────────────────────┘
                    │ TCP 8888 (FBEB + protobuf)
                    ▼
┌───────────────────────────────────────────────────────┐
│  bike-server（腾讯云 docker）                          │
│  asio tcp::acceptor  +  N 线程 io_context             │
│  Session: 读 FBEB 帧 → Router → Handler → 写 FBEB 帧  │
└─────┬───────────────────────────────┬─────────────────┘
      │                               │
      ▼                               ▼
┌──────────────┐               ┌──────────────┐
│  MySQL       │               │  Redis       │
│  userinfo    │               │  code:{m}    │ TTL 300s
│  account     │               │  session:{t} │ TTL 7d
│  account_    │               │              │
│   record     │               │              │
└──────────────┘               └──────────────┘
```

### 4.2 模块布局

```
shared_bike_refactor/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── proto/bike.proto
├── common/                  # server+client 共用静态库
│   ├── CMakeLists.txt
│   ├── include/bike/protocol.hpp     # FBEB Frame + encode/decode
│   ├── include/bike/errors.hpp
│   └── src/protocol.cpp
├── server/
│   ├── CMakeLists.txt
│   ├── src/main.cpp
│   ├── src/server.{hpp,cpp}          # asio acceptor
│   ├── src/session.{hpp,cpp}         # 单连接状态机
│   ├── src/router.{hpp,cpp}          # event_id -> handler
│   ├── src/handlers/
│   │   ├── mobile_code.{hpp,cpp}
│   │   ├── login.{hpp,cpp}
│   │   ├── recharge.{hpp,cpp}
│   │   ├── account_balance.{hpp,cpp}
│   │   └── list_records.{hpp,cpp}
│   ├── src/repo/
│   │   ├── user_repo.{hpp,cpp}       # 抽象接口
│   │   └── account_repo.{hpp,cpp}
│   ├── src/db/
│   │   ├── mysql_pool.{hpp,cpp}
│   │   ├── mysql_user_repo.{hpp,cpp}
│   │   └── mysql_account_repo.{hpp,cpp}
│   ├── src/cache/
│   │   ├── session_store.{hpp,cpp}   # 抽象接口
│   │   └── redis_session_store.{hpp,cpp}
│   ├── src/config.{hpp,cpp}          # toml++
│   ├── src/logging.{hpp,cpp}         # spdlog
│   ├── tests/
│   │   ├── test_protocol.cpp
│   │   ├── test_router.cpp
│   │   ├── test_handlers.cpp         # 用 InMemoryRepo + InMemorySessionStore
│   │   └── test_frame_roundtrip.cpp
│   └── config/server.toml
├── client/
│   ├── CMakeLists.txt
│   ├── src/main.cpp
│   ├── src/backend_client.{hpp,cpp}  # asio 同步 client + FBEB 帧
│   ├── src/session_model.{hpp,cpp}   # 持有 session_token
│   ├── src/mainwindow.{hpp,cpp,ui}
│   ├── src/views/login_view.{hpp,cpp,ui}
│   ├── src/views/wallet_view.{hpp,cpp,ui}
│   └── src/views/records_view.{hpp,cpp,ui}
├── docker/
│   ├── Dockerfile.server
│   ├── docker-compose.yml
│   ├── mysql-init/schema.sql
│   └── server.toml                   # 容器内用的配置
├── scripts/
│   ├── build.sh                      # 本地交叉编译验证（可选）
│   └── deploy.sh                     # scp + 远程 docker compose up
└── docs/superpowers/specs/2026-07-18-shared-bike-refactor-design.md
```

### 4.3 关键设计决定

| 决定 | 理由 |
|---|---|
| 单 io_context + N 线程 | 简单；MySQL/Redis 同步阻塞，靠 N 线程并发 |
| Session 类只做协议层 | 业务状态全在 MySQL/Redis，Session 无业务状态 |
| Handler 签名 `Response handle(const Request&, Ctx&)` | 纯函数，可单测，Ctx 注入 repo/store |
| Repository 与 SessionStore 都是抽象接口 | 测试用内存实现，生产用 MySQL/hiredis 实现 |
| `common/` 静态库 | server 和 client 共享 FBEB + protobuf，避免协议漂移 |
| protobuf 在 build 时由 CMake 调用 protoc 生成 | 不提交生成产物 |

## 5. 协议

### 5.1 FBEB 帧（与原项目兼容）

```
偏移  长度  字段        说明
0     4    magic      ASCII "FBEB"
4     2    event_id   u16 little-endian
6     4    length     i32 little-endian (= N)
10    N    payload    protobuf 字节流
```

**改进点**：编码/解码显式拼字节，不依赖 unaligned access；用 `std::uint16_t` / `std::int32_t`；最大负载限制保留 `MAX_MESSAGE_LEN = 372680`。

### 5.2 事件 ID（保留原值）

| EID | 名称 | 方向 |
|---|---|---|
| 0x01 | mobile_code_req | → |
| 0x02 | mobile_code_rsp | ← |
| 0x03 | login_req | → |
| 0x04 | login_rsp | ← |
| 0x05 | recharge_req | → |
| 0x06 | recharge_rsp | ← |
| 0x07 | account_balance_req | → |
| 0x08 | account_balance_rsp | ← |
| 0x09 | list_account_records_req | → |
| 0x10 | account_records_rsp | ← |
| 0xFE | exit_rsp | ←（保留） |

### 5.3 `proto/bike.proto`

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

## 6. 数据库 schema

文件：`docker/mysql-init/schema.sql`（首次启动 mysql 容器时自动执行）

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
  type          tinyint      NOT NULL,   -- 1=充值 2=消费
  amount        int          NOT NULL,
  balance_after int          NOT NULL,
  tm            timestamp    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX user_tm (user_id, tm)
);
```

> balance / amount 单位是「分」，整型避免浮点。

## 7. Redis schema

| 键 | 类型 | 值 | TTL | 用途 |
|---|---|---|---|---|
| `code:{mobile}` | string | 6 位验证码 | 300s | 验证码下发与校验 |
| `session:{token}` | string | mobile | 7d | 登录态 |

`token = uuid4()`，128-bit 随机，不可猜。

## 8. 横切关注点

### 8.1 错误处理

- Handler 内异常用 `std::variant<Response, Error>` 或抛 `bike::Error(code, msg)`，Router 统一捕获并构造错误响应
- 错误码沿用原项目 HTTP 风格：200/400/404/405/406，新增 `401 Unauthorized`（session 失效）
- 网络层错误（帧损坏等）→ 关闭连接 + WARN 日志

### 8.2 配置（`server/config/server.toml`）

```toml
[server]
listen = "0.0.0.0"
port = 8888
threads = 4

[mysql]
host = "mysql"        # docker compose service name
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

### 8.3 日志

spdlog，控制台 + 滚动文件，格式 `[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v`。

### 8.4 测试策略

- googleTest
- `common/`：协议编解码 round-trip
- `server/handlers/`：每个 handler 用 `InMemoryUserRepo` / `InMemoryAccountRepo` / `InMemorySessionStore` 单测
- `server/`：端到端测试，本地起 io_context + 内存 repo，模拟 client 发 FBEB 帧
- `client/`：暂不写自动化测试，手动验收

## 9. 部署

### 9.1 docker-compose.yml

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
    ports: ["3306:3306"]

  redis:
    image: redis:7-alpine
    ports: ["6379:6379"]

  server:
    build:
      context: ..
      dockerfile: docker/Dockerfile.server
    depends_on: [mysql, redis]
    ports: ["8888:8888"]
    volumes:
      - ./server.toml:/etc/bike/server.toml:ro
      - server-logs:/var/log/bike-server

volumes:
  mysql-data:
  server-logs:
```

### 9.2 部署流程（`scripts/deploy.sh`）

1. 本地 `docker build -t bike-server:$(git rev-parse --short HEAD) -f docker/Dockerfile.server .`
2. `docker save | gzip > /tmp/bike-server.tgz`
3. `scp /tmp/bike-server.tgz docker/ ubuntu@124.220.92.243:~/bike/`
4. ssh 远程：`docker load < bike-server.tgz && docker compose up -d`
5. 等待 healthy，`curl -sf localhost:8888` 不通则回滚

> 客户端要能从 Windows 直连 `124.220.92.243:8888`，腾讯云安全组需放行 8888 入站。

## 10. 客户端（Qt6）

### 10.1 单窗口 + 三个 view

```
MainWindow
  ├ LoginView    (mobile + icode 输入，发码/登录按钮)
  ├ WalletView   (余额显示，充值输入框)
  └ RecordsView  (QTableView 显示流水)
```

登录成功后 LoginView 切换到 WalletView；session_token 存在 `SessionModel`。

### 10.2 BackendClient

同步 asio（Qt 线程里用 QtConcurrent::run 包一下，避免阻塞 UI）：
- `MobileCodeRsp getMobileCode(string mobile)`
- `LoginRsp login(string mobile, int icode)`
- `RechargeRsp recharge(string token, int amount)`
- `AccountBalanceRsp getBalance(string token)`
- `ListRecordsRsp listRecords(string token)`

每个方法：序列化 protobuf → 封 FBEB 帧 → `asio::write` → 阻塞读 10B header → 读 body → 解析 → 返回。

## 11. 风险与外部依赖

| 风险 | 缓解 |
|---|---|
| 腾讯云未推公钥，无法 ssh 部署 | 部署前会显式提醒用户执行 `!ssh-copy-id ...` |
| GitHub 未添加本机 SSH 公钥 | 推送前会显式提醒用户到 GitHub Settings → SSH keys 粘贴 `~/.ssh/id_ed25519.pub` |
| 腾讯云安全组未放 8888 | 提醒用户到控制台开放 |
| Qt6 在 Windows 构建复杂 | 用 vcpkg 或预编译 Qt；客户端独立 CMake 项目，不阻塞后端 |
| protobuf 版本冲突 | Docker 内固定 protoc 3.21 + libprotobuf-dev |

## 12. 附录 A：原项目架构总结（迁移参考）

见对话历史中已交付的架构总结。核心点：
- libevent 单线程事件循环 + 自定义 pthread 线程池（4 worker）
- DispatchMsgService 单例 pub/sub 事件总线
- `iEevent` 基类（typo），`void*` 携带 ConnectSession 反向引用
- 仅 `UserEventHandler` 实现了 mobile_code + login；其它 handler 全是 stub
- `MysqlConnection` 无连接池；`UserService::exist/insert` 用 sprintf 拼 SQL（注入风险）
- 表 `userinfo` + `bikeInfo`（后者本次 scope 砍掉）
- 配置 INI：[database] ip/port/user/pwd/db + [server] port
- 日志 log4cpp（properties 配置文件）
