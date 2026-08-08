# Ops Runbook — 共享单车后端

## 部署

> **网关已切换到 io_uring(模块二)**: `bike-server` 构建目标已 gate 到 Linux-only,
> 宿主机/容器需 `liburing-dev`(Docker 镜像已内置)。Windows 本地无法编译
> io_uring 实体, 仅能构建并运行 gateway 纯逻辑测试(bike_gateway_core:
> 切帧/OutboxQueue/Sink)与 common/server 其余单测;
> bike-server 完整链接与运行验证一律在云服务器/Docker 内进行。

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
4. 验证 `bike-server` 容器已起:
   ```bash
   docker compose -f docker/docker-compose.yml ps
   docker compose -f docker/docker-compose.yml logs --tail=50 bike-server
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
- `RideSessionStore` 在内存中,server 重启会丢失所有活跃骑行会话
- 重启后:所有 `bike.status=1` (rented) 的车会卡死,用户无法再扫码该车
- `scan_unlock` 的 ride_no daily_seq 用 `now_unix() % 999999` 占位 —— 高并发下可能撞号,生产应换 Redis INCR
- `end_ride` 距离按起点↔终点直线计算,真实应从 `ride_position` 表累加折线长度
- `end_ride` 写入 `ride_position` 只存起点+终点两点,真实应累积 session 期间所有上报点
- 中国境内 GPS 坐标系问题(WGS84 vs GCJ-02)未处理 —— 客户端若用国内地图 SDK,上报前需做坐标转换

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

测试栈用独立端口(13306/16379)与生产隔离:

```bash
docker compose -f docker/docker-compose.test.yml up -d
# 跑一个指向测试栈的 bike-server 实例
BIKE_TEST_PORT=18888 ./build/server/bike-server --config=server/tests/integration/test_config.toml &
pytest server/tests/integration/ -v
docker compose -f docker/docker-compose.test.yml down
```

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
