# Cross-Region Bench + nginx Multi-Instance Design

**Date**: 2026-07-28
**Author**: hwy + Claude
**Status**: Draft (awaiting user review)

## 1. Background

Earlier performance work on the shared-bike server established:

- Baseline (single bike-server, sync dispatch): 17,452 QPS @ c=100, P99=7.86ms
- After async thread-pool refactor (biz=8): 19,925 QPS @ c=500, P99=32.4ms
- biz=16 tuned as production setting (sweet spot at CPU × 4)
- All numbers measured from inside the server host (loopback), so they
  reflect server-side processing capacity but not real user latency

Two threads of follow-up work motivate this spec:

1. **Public-Internet path**: Every previous number was loopback. Real users
   hit the server over the public Internet, with RTT and bandwidth overhead
   that the loopback bench cannot surface.
2. **Horizontal scaling story**: The async refactor proved the bottleneck
   is downstream synchronization, not the asio network layer. A natural
   next question is whether the service can scale out across multiple
   instances behind a reverse proxy.

This spec designs a single integrated benchmark that answers both:
how does the server behave under cross-region load, and does putting
nginx in front of multiple bike-server instances move the ceiling?

## 2. Goals

- **G1**: Fill in the missing biz=16 loopback numbers at c=100/500
  (skipped in the earlier biz sweep). Confirms whether 19,925 QPS was
  noise or repeatable.
- **G2**: Measure server behavior under realistic public-Internet load
  (Windows host → Tencent Cloud CVM), including RTT and bandwidth effects.
- **G3**: Stand up an nginx reverse proxy in front of 2 bike-server
  instances on the same host and measure whether total throughput
  changes vs. single instance. Expected answer: no improvement
  (CPU-bound), but the data proves it rather than asserting it.
- **G4**: Produce a coherent writeup that can be cited verbatim in a
  resume and defended in an interview.

## 3. Non-Goals

- Multi-host horizontal scaling (only 1 CVM available).
- HTTPS / TLS termination (the wire protocol is binary FBEB, not HTTP).
- nginx rate limiting, auth, or rewrite rules (out of scope; this is
  purely a TCP load-distribution test).
- Client-side connection pooling across processes (each worker has its
  own connection set; that is the point).

## 4. Architecture

### 4.1 End-to-end topology

```
Windows host (D:\C++\shared_bike_1)
├── python scripts/bench/bike_bench.py --worker-id 0 --total-workers 4 -c 500
├── python scripts/bench/bike_bench.py --worker-id 1 --total-workers 4 -c 500
├── python scripts/bench/bike_bench.py --worker-id 2 --total-workers 4 -c 500
└── python scripts/bench/bike_bench.py --worker-id 3 --total-workers 4 -c 500
        each process owns 500 persistent connections
        total: 2000 concurrent connections, 4 OS processes
                │
                │   public Internet, 3 MB/s measured bandwidth
                ▼
Tencent Cloud CVM (124.220.92.243, 4 vCPU AMD EPYC 7K62)
                │
                ▼   TCP :8888
        ┌──────────────────────┐
        │ nginx container      │  (new)
        │  worker_processes=4  │
        │  worker_connections  │
        │   =65535             │
        │  upstream least_conn │
        └─────┬──────────┬─────┘
              │          │
              ▼          ▼
       bike-server-1  bike-server-2     (new: 2nd instance)
       (172.18.0.4)   (172.18.0.5)
       io=4 biz=16    io=4 biz=16
              │          │
              └────┬─────┘
                   ▼
        shared mysql + redis containers
```

### 4.2 Why nginx in front of two instances on the same host

A reasonable reviewer might ask: why bother? The host has 4 vCPU, and
a single bike-server already spawns 4 io workers + 16 biz workers = 20
threads. Adding a second instance doubles thread count to 40 on a
4-vCPU machine — context-switch overhead alone should make this worse,
not better.

That is exactly the point. The experiment is designed to **prove** the
hypothesis that the bottleneck is CPU, not connection handling. The
acceptance tolerance is ±15% (AC5): within that band, the data backs
the "single instance already saturates the host" narrative. If QPS
improves beyond +15%, that is even more interesting — it would mean
the asio event loop has serialization we did not see.

Either outcome is a defensible interview talking point.

### 4.3 Why least_conn and not round_robin

The two bike-server instances are identical, but requests differ in
cost (mobile_code = 1 Redis op; login chain = 2 Redis + MySQL).
`least_conn` routes new connections to whichever backend currently
has the fewest in-flight connections, which over time produces better
load balancing than naive round_robin when request cost is uneven.

## 5. Implementation Plan

### 5.1 Phase 1: loopback gap-fill (single instance, no nginx)

This phase runs entirely on the server. No nginx yet, no public
traffic. Goal: confirm biz=16 numbers at the missing concurrencies.

| Step | Action |
|------|--------|
| 1.1  | Add `--worker-id` and `--total-workers` flags to `bike_bench.py`. Workers partition the mobile-number range by `worker_id`, so concurrent processes don't collide on the same Redis key. |
| 1.2  | Add a `--json-out <path>` flag that writes final stats as a single JSON line. |
| 1.3  | Single-worker loopback bench on the server: `bike_bench.py qps -c 100 -d 20` and `-c 500 -d 20`, biz=16. This is the same command pattern as before, just the missing concurrencies. |

### 5.2 Phase 2: nginx + 2 instances deployment

| Step | Action |
|------|--------|
| 2.1  | Modify `docker/docker-compose.yml`: remove `ports: "8888:8888"` from `server`, replace with `expose: ["8888"]`. Add `container_name: bike-server-1` and a `bike-server-2` duplicate pointing at the same image but a different container_name and BIKE_INSTANCE_ID env var (for log disambiguation). |
| 2.2  | Add `nginx` service to compose. Mount a generated `nginx.conf` that listens on 8888 and proxies to `http://bike-server-1:8888` and `http://bike-server-2:8888` via least_conn upstream. Use `nginx:1.27-alpine` image. |
| 2.3  | Write `docker/nginx.conf`: set `worker_processes auto`, `worker_connections 65535`, `upstream bike_backend { least_conn; server bike-server-1:8888 max_fails=3 fail_timeout=5s; server bike-server-2:8888 max_fails=3 fail_timeout=5s; }`, `location / { proxy_pass http://bike_backend; }`. Note: nginx streams TCP, not HTTP — see 2.4. |
| 2.4  | nginx must use the **stream** module, not HTTP proxy, because the FBEB wire protocol is raw TCP, not HTTP. Switch config to `stream { upstream bike_backend { ... } server { listen 8888; proxy_pass bike_backend; } }`. |
| 2.5  | `docker compose up -d --force-recreate`. Verify with a manual round-trip from the server host: `python3 /tmp/bike_bench.py rtt -c 1`. |
| 2.6  | Smoke check: `docker logs bike-server-1` and `bike-server-2` should both show request logs. |

### 5.3 Phase 3: cross-region public-Internet bench

| Step | Action |
|------|--------|
| 3.1  | Windows host: bump ephemeral port range. `netsh int ipv4 set dynamicport tcp start=10000 num=55535`. |
| 3.2  | Windows host: bump fd limit (the Python process is bounded by the OS). Verify with a quick `test_socket_overflow.py` that opens 6000 sockets. |
| 3.3  | Run **single-instance** bench first by temporarily routing nginx upstream to only `bike-server-1`. This gives a clean public-Internet baseline for one instance without nginx in the path. |
| 3.4  | Run **nginx + 2-instance** bench: re-enable both backends in upstream, repeat the same c sweep. |
| 3.5  | For each scenario, launch 4 `bike_bench.py` workers from PowerShell with `Start-Process` (or a small `run_bench.ps1`), wait for all to exit, then aggregate the 4 JSON files into a merged report. |
| 3.6  | Aggregator computes: total QPS (sum of worker QPS), latency merge across workers (concat all samples, sort, percentiles), bandwidth estimate from total bytes / duration. |

### 5.4 Phase 4: writeup

| Step | Action |
|------|--------|
| 4.1  | Produce `docs/superpowers/specs/2026-07-28-cross-region-bench-nginx-results.md` with side-by-side tables: loopback single / public single / public nginx+2. |
| 4.2  | Note any surprises or follow-ups in the writeup (e.g., if nginx latency overhead is non-trivial, if 2 instances regress, if bandwidth turns out to be the limit). |

## 6. Test Matrix

### 6.1 Port mapping

| Service | Container → Host | Reachable from |
|---------|------------------|----------------|
| bike-server-1 | 8888 (internal) | nginx upstream only |
| bike-server-2 | 8888 (internal) | nginx upstream only |
| nginx | 0.0.0.0:8888 → 8888 | public Internet |

The single-instance public scenario (3A/3B/3C/3D -single) is achieved
by setting nginx upstream to contain only `bike-server-1`. This keeps
nginx in the data path for both scenarios, so the only variable is the
backend count. The cost of this choice is that nginx's own overhead
(an extra TCP relay hop in-stream) is folded into both — so the
"single vs nginx+2" comparison reflects backend scaling, not nginx
cost. nginx's standalone overhead can be estimated separately by
comparing Phase 1 loopback (no nginx) to a hypothetical Phase 3
"direct-to-bike-server-1" run; we capture this in the writeup as a
side note rather than a separate matrix row, since exposing
bike-server-1 directly to the public Internet would be a security
regression for marginal analytical gain.

### 6.2 Cells

Each cell is a 20-second run. `c` is concurrency **per worker**; total
concurrency = c × 4 in public-Internet scenarios.

| Phase | Topology | Workers | c/worker | Total c |
|-------|----------|---------|----------|---------|
| 1A | loopback, single instance | 1 (on server) | 100 | 100 |
| 1B | loopback, single instance | 1 (on server) | 500 | 500 |
| 3A-single | public, single instance | 4 (on Windows) | 25 | 100 |
| 3B-single | public, single instance | 4 (on Windows) | 125 | 500 |
| 3C-single | public, single instance | 4 (on Windows) | 250 | 1000 |
| 3D-single | public, single instance | 4 (on Windows) | 500 | 2000 |
| 3A-nginx | public, nginx + 2 inst | 4 (on Windows) | 25 | 100 |
| 3B-nginx | public, nginx + 2 inst | 4 (on Windows) | 125 | 500 |
| 3C-nginx | public, nginx + 2 inst | 4 (on Windows) | 250 | 1000 |
| 3D-nginx | public, nginx + 2 inst | 4 (on Windows) | 500 | 2000 |

Plus a 5-second smoke run before each scenario to verify the path works.

## 7. Acceptance Criteria

| ID | Criterion |
|----|-----------|
| AC1 | Phase 1A loopback QPS at c=100 is within ±10% of the earlier 18,838 (biz=8 number) — sanity that nothing regressed. |
| AC2 | Phase 1B loopback QPS at c=500 ≥ 18,000, confirming the earlier 19,925 was not noise. |
| AC3 | Phase 3A-single public QPS at total c=100 ≥ 1,000 (lower bound — public RTT will dominate). |
| AC4 | Phase 3D-single public P99 ≤ 200ms (RTT + processing). |
| AC5 | Phase 3D-nginx QPS within ±15% of Phase 3D-single (the expected "no real improvement" result). |
| AC6 | All scenarios: errors < 1% of total requests. |
| AC7 | Aggregated JSON report exists and is internally consistent (sum of per-worker QPS within 5% of total requests / duration). |

## 8. Risks and Mitigations

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| 3 MB/s public bandwidth caps QPS well below server capacity | High | Run loopback Phase 1 first to establish the server-side ceiling. If public QPS ≪ loopback, attribute the gap to bandwidth and document it. |
| Windows home broadband upstream is far below 3 MB/s | Medium | Run a 60-second iperf3 TCP test from Windows → server before benching. If upstream < 1 MB/s, document and proceed with the actual number. |
| 2 bike-server instances fight over 4 vCPU and regress | High | This is expected. AC5 tolerates ±15%. |
| nginx stream module config wrong, all requests fail | Medium | Step 2.5 smoke test before any real bench. |
| Windows ephemeral port exhaustion at c=2000 × 4 workers | Medium | Step 3.1 explicitly widens the port range. Verify with a quick socket test. |
| Existing docker-server-1 (running for 10+ days) gets disrupted | Low | We force-recreate as part of the deploy — already tested, no data loss since state lives in mysql/redis volumes. |
| Public-Internet packet loss inflates P99 unpredictably | Medium | Run each scenario twice; report the lower-variance run. Note in writeup if the two runs diverge > 20%. |

## 9. Out-of-Scope Follow-Ups

Captured here so they are not forgotten but not done in this pass:

- Build a real iperf3 baseline to attribute bandwidth vs. server bottlenecks.
- Try nginx + 4 instances (each with io=2/biz=4) to see if reduced per-instance thread count helps when instances double.
- Move one bike-server to a second host (would require provisioning).
- Add per-handler QPS breakdown (mobile_code vs login vs list_records).

## 10. Deliverables

- Modified `scripts/bench/bike_bench.py` with `--worker-id`, `--total-workers`, `--json-out`.
- New `scripts/bench/run_bench.ps1` PowerShell launcher that spawns N workers and aggregates.
- New `docker/nginx.conf`.
- Modified `docker/docker-compose.yml` with bike-server-2 + nginx services.
- New results doc `docs/superpowers/specs/2026-07-28-cross-region-bench-nginx-results.md`.
- Updated resume numbers if any of the new data supersedes prior claims.

## 11. Resume-Facing Summary (intended final paragraph)

> 性能压测与水平扩展验证: 基于自研 Python asyncio 压测客户端(支持多 worker
> 进程聚合, 复现 FBEB+Protobuf 二进制协议), 在 4C3.6G 腾讯云 CVM 上从
> 服务器内网与跨地域公网两条路径压测:
>
> - 内网单实例: 吞吐峰值 19,900+ QPS (c=500, P99=32ms, 0 错误)
> - 公网跨地域 (Windows → Tencent Cloud, 3 MB/s 带宽):
>   单实例 [X] QPS @ P99=[Y]ms, 2000 并发 0 错误
> - nginx stream 反向代理 + 2 bike-server 实例水平扩展:
>   QPS 变化 [±Z]%, 证明单实例已饱和 4 vCPU, 单机水平扩展受物理 CPU 限制
>
> 通过控制变量(内网 vs 公网 / 单实例 vs 多实例 / biz=8 vs 16 vs 32)
> 定位瓶颈从连接池 → io 线程同步阻塞 → 物理 CPU, 形成完整的性能分析链.
