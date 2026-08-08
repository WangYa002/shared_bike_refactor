# Ops Runbook — 共享单车后端

## 部署

> **服务端已切换为双进程拓扑(模块三)**: `bike-gateway`(io_uring 纯网关:
> 接入/切帧/协议解析) + `bike-dispatch`(业务: Router/全部 repo/RideSessionStore),
> 两者经 /dev/shm 上的 mmap SPSC 环 + FIFO 通知连接; `[ipc].mode="inprocess"`
> 可回退单进程形态(进程内直连 Router, 不需 bike-dispatch)。
> 两个构建目标均 gate 到 Linux-only, 宿主机/容器需 `liburing-dev`
> (Docker 镜像已内置)。Windows 本地无法编译 io_uring/IPC 实体,
> 仅能构建并运行 gateway 纯逻辑测试(bike_gateway_core: 切帧/OutboxQueue/Sink)、
> IPC 纯逻辑测试(spsc_ring/ipc_packet)与 common/server 其余单测;
> bike-gateway/bike-dispatch 完整链接与运行验证一律在云服务器/Docker 内进行。

1. SSH 到 Tencent Cloud: `ssh ubuntu@124.220.92.243`
2. 在服务器上拉最新代码:
   ```bash
   cd ~/shared_bike
   git pull
   ```
3. 重新构建并启动栈:
   ```bash
   docker compose -f docker/docker-compose.yml up -d --build
   ```
4. 验证网关与 Dispatch 容器均已起(每实例一对: bike-server-N = gateway,
   bike-dispatch-N = 业务; 容器名 bike-server-N 为历史沿用):
   ```bash
   docker compose -f docker/docker-compose.yml ps
   docker compose -f docker/docker-compose.yml logs --tail=50 bike-server-1
   docker compose -f docker/docker-compose.yml logs --tail=50 bike-dispatch-1
   ```
5. 用 Python 客户端打生产端口(8888)验证 `LIST_NEARBY_BIKES` 返回 60 辆车:
   ```bash
   # 一次性生成 bike_pb2.py
   protoc --python_out=server/tests/integration/_proto --proto_path=proto proto/bike.proto
   # 然后跑一个最小化的 ping(可写成 ops_ping.py 或手敲)
   ```

## 已知限制

- **空闲连接强关**: 网关每轮事件循环检查连接空闲时长, 超过 `[uring].idle_timeout_ms`
  (默认 60000ms) 未收到任何字节的连接会被服务端主动断开。
  客户端侧表现为: 长时间无请求后下一次操作会报"网络异常"并重连, 属预期行为;
  需要长驻的连接应在 60s 内保持有请求往来(或调大配置)。
- `RideSessionStore` 在内存中且归属 **bike-dispatch** 进程: Dispatch(或整套)重启
  会丢失所有活跃骑行会话
- 重启后:所有 `bike.status=1` (rented) 的车会卡死,用户无法再扫码该车
- **Dispatch 判死 → Gateway 自退**: ring 模式下任一侧检测到对端心跳超时/pid 死亡,
  本进程主动退出由 compose 重拉整套(不做运行时热重附着); 短暂窗口内新连接会失败,
  客户端需重连(已有重连逻辑)。
- **SinkOverload 关连**(ring 模式新增的客户端可见行为): 请求环满时网关直接关闭
  该连接做背压, 客户端表现为"网络异常"重连; 频繁出现说明 Dispatch 消费跟不上,
  应调大 `[ipc].dispatch_workers` 或增加实例。
- `scan_unlock` 的 ride_no daily_seq 用 `now_unix() % 999999` 占位 —— 高并发下可能撞号,生产应换 Redis INCR
- `end_ride` 距离按起点↔终点直线计算,真实应从 `ride_position` 表累加折线长度
- `end_ride` 写入 `ride_position` 只存起点+终点两点,真实应累积 session 期间所有上报点
- 中国境内 GPS 坐标系问题(WGS84 vs GCJ-02)未处理 —— 客户端若用国内地图 SDK,上报前需做坐标转换

## 模块三: 双进程运维

### 双进程重启顺序

- **Gateway 与 Dispatch 必须成对重启**: 两进程共享 /dev/shm 环与心跳判活,
  单侧重启会触发对端判死自退(由 compose 重拉整套), 属预期但会短暂中断。
- **`[uring].workers`(网关 worker 数)变更必须 Gateway+Dispatch 成对重启**:
  请求环个数 = worker 数, 写进 shm 文件头, 不一致会直接重建环或校验失败。
  改完 toml/环境变量后: `docker compose up -d --build`(两侧一起)。
- 滚动升级建议逐实例操作: 先 `stop bike-server-2 bike-dispatch-2` → 升级重拉 →
  健康后再动实例 1(nginx least_conn 会自动摘掉已停实例)。

### shm 兜底清理

- compose 用 256MB tmpfs 卷挂 /dev/shm(每实例独立), 容器重建即自动清空,
  正常运维**无需手动清理**。
- 裸机部署或环文件疑似损坏(启动反复 reinit/校验失败)时兜底:
  ```bash
  # 先停掉 bike-gateway/bike-dispatch, 再清理(环文件由 shm_open 固定落 /dev/shm)
  rm /dev/shm/bike*          # 含 bike{instance}_req / bike{instance}_rsp
  rm /tmp/bike*_notify /tmp/bike-dispatch*.alive   # FIFO 与健康文件
  ```
- tmpfs 容量说明: 单实例 ≈ 请求环 N×2MB + 响应环 ~98MB, 默认 256MB 足够
  16 worker 单实例; 加大 worker 数或双实例共卷时需同步调大 tmpfs size。

### healthcheck / alive 文件

- bike-dispatch 每 1s touch `/tmp/bike-dispatch{instance}.alive`(compose 环境变量
  BIKE_INSTANCE 决定后缀)。
- compose healthcheck 为**存在性+新鲜度复合检查**: 文件存在且 6s 内更新过
  (`find ... -mmin -0.1`)才算健康 —— 防进程卡死(死循环/GC 级阻塞)后
  文件残留被误判为健康。
- Gateway 容器无 healthcheck: 其存活性由 Dispatch 判活联动(对端死 → 自退重拉),
  且依赖 Dispatch 先 healthy(`depends_on: service_healthy`)。

### [ipc] 调参表

| 键 | 默认 | 说明 |
|---|---|---|
| mode | ring | `ring`=双进程; `inprocess`=回退单进程(不需 bike-dispatch) |
| shm_root | /dev/shm | FIFO 所在目录(shm 文件由 shm_open 固定落 /dev/shm) |
| shm_prefix | bike | shm/FIFO 文件名前缀: {prefix}{instance}_req 等 |
| instance | 0 | 实例号; BIKE_INSTANCE 环境变量可覆盖(双进程必须一致) |
| open_timeout_ms | 10000 | Gateway 等 Dispatch 建环上限, 超时 fail-fast 重拉 |
| peer_timeout_ms | 5000 | 对端心跳超时 → 判死 → 本进程退出重拉 |
| spin_tries | 64 | 读方睡眠前自旋次数(延迟 vs CPU 折中) |
| dispatch_workers | 8/16 | Dispatch 业务线程数 M |
| req_ring_slots | 512 | **编译期常量约束**: 必须 == ReqRing::kSlotCount, 不一致 load_config 报错 |
| rsp_ring_slots | 256 | **编译期常量约束**: 必须 == RspRing::kSlotCount |

> 槽数/槽大小 v1 为编译期常量(避免双进程不一致), 仅做配置一致性校验;
> 改容量需改 `shm_layout.hpp`/`spsc_ring.hpp` 并**双进程成对重编译重启**。

## 恢复 stuck 单车(运维手动操作)

server 异常重启后,执行以下 SQL 把卡在 rented 状态的车改回 idle:

```sql
UPDATE bike SET status = 0
WHERE status = 1
  AND id NOT IN (
    SELECT bike_id FROM ride
    WHERE status = 0
      AND end_tm > DATE_SUB(NOW(), INTERVAL 2 HOUR)
  );
```

执行前请确认:
- 当前没有真正活跃的骑行(`ride_sessions` 已被 server 重启清空,所以查最近 2 小时内的 `ride` 表里 end_tm 不为 NULL 但已结束的行程)
- 此操作不可逆,执行后任何进行中的扫码车辆记录都会变成孤儿(没有 active session 但 bike 已 idle)

## 集成测试栈

测试栈用独立端口(13306/16379)与生产隔离。旧 `bike-server` 单二进制已不存在,
按下列两种方式之一起服务(`--config=` 旗标也已改为位置参数):

```bash
docker compose -f docker/docker-compose.test.yml up -d

# 方式 A: ring 模式双进程(与生产拓扑一致; Dispatch 先建环, Gateway 后附着)
BIKE_INSTANCE=9 ./build/server/bike-dispatch server/tests/integration/test_config.toml &
BIKE_INSTANCE=9 ./build/server/bike-gateway  server/tests/integration/test_config.toml &

# 方式 B: inprocess 回退单进程(测试 toml 设 [ipc].mode="inprocess", 不需 Dispatch)
./build/server/bike-gateway server/tests/integration/test_config.toml &

pytest server/tests/integration/ -v
docker compose -f docker/docker-compose.test.yml down
```

注: 测试 toml 需把 `[server].port` 指到测试端口(如 18888)、`[ipc].shm_root`
指到可写目录(裸机跑时 shm_open 仍固定落 /dev/shm); 方式 A 的两个进程
BIKE_INSTANCE 必须一致, 测试完按上节"shm 兜底清理"删除残留。

## 端口/事件 ID 速查

> 事件号以 `bike::Event` 枚举(`common/include/bike/protocol.hpp`)为唯一事实来源,
> 下表仅为速查; 不一致时以枚举为准。

| Event ID | Direction | Name |
|---|---|---|
| 0x01 / 0x02 | req/rsp | mobile_code |
| 0x03 / 0x04 | req/rsp | login |
| 0x05 / 0x06 | req/rsp | recharge |
| 0x07 / 0x08 | req/rsp | account_balance |
| 0x09 / 0x10 | req/rsp | list_records |
| 0x11 / 0x12 | req/rsp | list_nearby_bikes |
| 0x13 / 0x14 | req/rsp | scan_unlock |
| 0x15 | oneway | position_report |
| 0x17 / 0x18 | req/rsp | end_ride |
| 0x19 / 0x1A | req/rsp | report_damage |
| 0x1B / 0x1C | req/rsp | get_ride_detail |
| 0x1D / 0x1E | req/rsp | list_rides |
