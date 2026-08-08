# shared_bike_refactor

C++17 refactor of a shared-bike backend + Qt6 desktop client.

## Layout

- `legacy/` — original C++03 source (libevent + protobuf + log4cpp), preserved for reference
- `proto/` — protobuf schema
- `common/` — shared static lib: FBEB protocol + errors
- `server/` — backend, 双二进制双进程形态 (模块三): `bike-gateway`(io_uring 纯网关:
  接入/切帧/协议解析) + `bike-dispatch`(业务: Router/repo/会话), 经 /dev/shm 上的
  mmap SPSC IPC 环 + FIFO 通知连接; `[ipc].mode="inprocess"` 可回退单进程形态。
  Linux-only binaries (io_uring + hiredis + mysql + spdlog + toml++)
- `client/` — Qt6 desktop client
- `docker/` — Dockerfile + docker-compose + mysql-init
- `docs/superpowers/` — design + plan

## Build & run

See `docs/superpowers/specs/2026-07-18-shared-bike-refactor-design.md` for the full design.
See [docs/ops.md](docs/ops.md) for the ops runbook (deploy steps, stuck-bike recovery, test stack, event ID table).

### Backend (in Docker)

    docker compose -f docker/docker-compose.yml up --build

每实例一对容器: `bike-dispatch-N`(业务, 先启动建环) + `bike-server-N`(bike-gateway
纯网关, 容器名为历史沿用); gateway 在容器内监听 `0.0.0.0:8888`, 经 nginx
stream 代理映射到宿主机 8888。详见 [docs/ops.md](docs/ops.md)。

### Client (Qt6 + CMake on Windows)

    cmake -B build -S . -DBIKE_BUILD_CLIENT=ON
    cmake --build build --config Release --target bike_client

Then launch `build/client/Release/bike_client.exe` (or platform equivalent).

## Wire protocol (v2)

> **破坏性变更**: 帧头由 10 字节升级为 14 字节(新增 `seq` u32 LE)。
> server / client / bench / 集成脚本必须**原子协同发布**(同一次部署同时更新),
> 旧客户端(10 字节帧头)连新服务端会因 magic/长度校验失败被快速断开, 不兼容。

    +4 bytes  magic       ASCII "FBEB"
    +2 bytes  event_id    u16 little-endian
    +4 bytes  seq         u32 little-endian (客户端每连接自增, 服务端原样回带)
    +4 bytes  length      i32 little-endian (= N)
    +N bytes  payload     serialized protobuf

Event IDs:
| 0x01 / 0x02 | mobile_code_req / rsp |
| 0x03 / 0x04 | login_req / rsp |
| 0x05 / 0x06 | recharge_req / rsp |
| 0x07 / 0x08 | account_balance_req / rsp |
| 0x09 / 0x10 | list_account_records_req / rsp |

See `proto/bike.proto` for message schemas.
