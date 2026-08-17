#!/usr/bin/env python3
"""auto_bench.py — 自动阶梯升压压测: 从低并发逐级翻倍, 自动找到服务端最大 QPS。

单文件零依赖(纯 asyncio + struct + json), Windows/Linux 通用。
多 worker 子进程铺满客户端多核, 多端口(8881/8882)均分连接压双后端实例。

用法:
  python auto_bench.py --host 10.0.0.14                       # 全默认: 2 worker x 2 端口
  python auto_bench.py --host 10.0.0.14 --workers 4 --max-c 4096
  python auto_bench.py --host 10.0.0.14 --connect-only         # 仅做连通性检查

停止条件(任一满足即收口并报告峰值):
  1. 本级 QPS 相比上一级提升 < 5% (吞吐平台 = 服务端饱和)
  2. 错误率 > 1% 或建连失败 > 10%
  3. 并发达 --max-c (默认 2048)
"""
import argparse
import asyncio
import json
import os
import statistics
import struct
import subprocess
import sys
import tempfile
import time

MAGIC = b'FBEB'
HEADER_LEN = 14
MAX_MSG_LEN = 372680

# ---------------- FBEB + protobuf minimal codec (与 bike_bench.py 一致) ----------------

def encode_varint(n: int) -> bytes:
    out = bytearray()
    n &= 0xFFFFFFFFFFFFFFFF
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)

def encode_string(field: int, s: str) -> bytes:
    b = s.encode('utf-8')
    return encode_varint((field << 3) | 2) + encode_varint(len(b)) + b

def encode_frame(eid: int, payload: bytes, seq: int = 0) -> bytes:
    return MAGIC + struct.pack('<HIi', eid, seq, len(payload)) + payload

async def read_frame(reader):
    hdr = await reader.readexactly(HEADER_LEN)
    if hdr[:4] != MAGIC:
        raise ValueError(f"bad magic: {hdr[:4]!r}")
    length, = struct.unpack('<i', hdr[10:14])
    if length < 0 or length > MAX_MSG_LEN:
        raise ValueError(f"bad length: {length}")
    body = await reader.readexactly(length) if length > 0 else b''
    return body

def decode_mobile_ok(body: bytes) -> bool:
    """mobile_response { int32 code=1; int32 icode=2; } — code==200 且 icode!=0 才算成功"""
    pos = 0; code = 0; icode = 0
    while pos < len(body):
        tag, pos = _dec_varint(body, pos)
        f, wt = tag >> 3, tag & 7
        if wt == 0:
            v, pos = _dec_varint(body, pos)
            if f == 1: code = v
            elif f == 2: icode = v
        elif wt == 2:
            ln, pos = _dec_varint(body, pos)
            pos += ln
    return code == 200 and icode != 0

def _dec_varint(buf, pos):
    val = 0; shift = 0
    while True:
        b = buf[pos]; pos += 1
        val |= (b & 0x7F) << shift
        if not (b & 0x80):
            return val & 0xFFFFFFFF, pos
        shift += 7

# ---------------- worker: 单进程压一级并发 ----------------

def make_mobile(worker_id: int, total_workers: int, i: int) -> str:
    return "159%08d" % (worker_id * 1000000 + i)   # 每 worker 百万号段, 防撞 Redis key

async def run_level(host: str, ports: list, concurrency: int, duration: int,
                    worker_id: int, total_workers: int, out_path: str):
    """concurrency 条长连接均分到 ports, 每连接串行打 mobile_code, duration 秒。"""
    if sys.platform == 'win32':
        try:
            asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
        except AttributeError:
            pass

    latencies, errors = [], []
    connect_errors = []
    connected = 0
    stop = asyncio.Event()

    async def one(i: int):
        nonlocal connected
        host_port = (host, ports[i % len(ports)])
        mobile = make_mobile(worker_id, total_workers, i)
        try:
            reader, writer = await asyncio.open_connection(*host_port)
        except Exception as e:
            connect_errors.append(f"{host_port[1]}: {type(e).__name__}")
            return
        connected += 1
        try:
            while not stop.is_set():
                t0 = time.perf_counter()
                try:
                    writer.write(encode_frame(0x01, encode_string(1, mobile)))
                    await writer.drain()
                    body = await read_frame(reader)
                    if not decode_mobile_ok(body):
                        errors.append("bad_response")
                        continue
                    latencies.append((time.perf_counter() - t0) * 1000.0)
                except Exception as e:
                    errors.append(type(e).__name__)
                    return
        finally:
            try:
                writer.close()
            except Exception:
                pass

    t0 = time.perf_counter()
    tasks = [asyncio.create_task(one(i)) for i in range(concurrency)]
    deadline = time.perf_counter() + 30.0
    while time.perf_counter() < deadline:
        if connected + len(connect_errors) >= concurrency:
            break
        await asyncio.sleep(0.05)
    conn_dur = time.perf_counter() - t0

    if connected == 0:
        stop.set()
        await asyncio.gather(*tasks, return_exceptions=True)
        msg = "; ".join(sorted(set(connect_errors))[:3])
        print(f"  !! 无法连接 {host}:{ports} — {msg}", flush=True)
        print("  !! 排查: 1)服务端是否监听 2)云安全组/防火墙是否放行端口 3)网络是否可达", flush=True)
        json.dump({"worker_id": worker_id, "qps": 0, "requests": 0, "errors": 0,
                   "connected": 0, "connect_errors": len(connect_errors),
                   "samples": [], "conn_fail_reason": msg},
                  open(out_path, 'w'))
        return

    await asyncio.sleep(duration)
    stop.set()
    await asyncio.gather(*tasks, return_exceptions=True)
    elapsed = time.perf_counter() - t0 - conn_dur

    json.dump({"worker_id": worker_id, "qps": len(latencies) / elapsed if elapsed > 0 else 0,
               "requests": len(latencies), "errors": len(errors),
               "connected": connected, "connect_errors": len(connect_errors),
               "conn_dur": conn_dur, "samples": latencies},
              open(out_path, 'w'))
    print(f"  [worker {worker_id}] 建连 {connected}/{concurrency} ({conn_dur:.1f}s), "
          f"QPS {len(latencies)/max(elapsed,0.001):,.0f}, 错误 {len(errors)}", flush=True)

# ---------------- 主进程: 阶梯调度 + 聚合 ----------------

def aggregate(paths: list, total_c: int) -> dict:
    samples, qps, reqs, errs, conn_fail = [], 0, 0, 0, 0
    for p in paths:
        d = json.load(open(p))
        samples.extend(d.get("samples", []))
        qps += d.get("qps", 0)
        reqs += d.get("requests", 0)
        errs += d.get("errors", 0)
        conn_fail += d.get("connect_errors", 0)
    samples.sort()

    def pct(p):
        if not samples: return 0.0
        return samples[min(len(samples) - 1, int(len(samples) * p / 100))]

    return {"concurrency": total_c, "qps": qps, "requests": reqs, "errors": errs,
            "connect_failed": conn_fail,
            "p50": pct(50), "p90": pct(90), "p99": pct(99)}

def main():
    ap = argparse.ArgumentParser(description='自动阶梯升压压测')
    ap.add_argument('--host', required=True)
    ap.add_argument('--ports', default='8881,8882', help='逗号分隔, 连接均分(默认 8881,8882)')
    ap.add_argument('--workers', type=int, default=2, help='客户端子进程数(默认 2, 按客户端核数调)')
    ap.add_argument('--start-c', type=int, default=64, help='起始总并发(默认 64)')
    ap.add_argument('--max-c', type=int, default=2048, help='最大总并发(默认 2048)')
    ap.add_argument('-d', '--duration', type=int, default=15, help='每级持续秒数(默认 15)')
    ap.add_argument('--gain-stop', type=float, default=0.05, help='QPS 提升低于此比例即停(默认 0.05)')
    ap.add_argument('--connect-only', action='store_true', help='只做连通性检查后退出')
    ap.add_argument('--role', default='main', help=argparse.SUPPRESS)   # 内部: worker 子进程
    ap.add_argument('--level-c', type=int, default=0, help=argparse.SUPPRESS)
    ap.add_argument('--worker-id', type=int, default=0, help=argparse.SUPPRESS)
    ap.add_argument('--total-workers', type=int, default=1, help=argparse.SUPPRESS)
    ap.add_argument('--out', default='', help=argparse.SUPPRESS)
    args = ap.parse_args()
    ports = [int(p) for p in str(args.ports).split(',') if p.strip()]

    if args.role == 'worker':
        asyncio.run(run_level(args.host, ports, args.level_c // args.total_workers,
                              args.duration, args.worker_id, args.total_workers, args.out))
        return

    print(f"=== auto_bench → {args.host} ports={ports} workers={args.workers} "
          f"阶梯 {args.start_c}→{args.max_c} 每级 {args.duration}s ===\n", flush=True)

    # 连通性预检: 单连接 3 秒
    tmp = os.path.join(tempfile.gettempdir(), f"ab_conn_{os.getpid()}.json")
    r = subprocess.run([sys.executable, __file__, '--role', 'worker', '--host', args.host,
                        '--ports', ','.join(map(str, ports)), '--level-c', str(len(ports)),
                        '--duration', '3', '--worker-id', '0', '--total-workers', '1',
                        '--out', tmp],
                       capture_output=True, text=True, timeout=60)
    conn = json.load(open(tmp)) if os.path.exists(tmp) else {"connected": 0}
    os.unlink(tmp)
    if conn.get("connected", 0) < len(ports):
        print(r.stdout)
        print("!! 连通性检查失败 — 请先打通网络再压测", flush=True)
        sys.exit(1)
    print(f"[连通性 OK] {args.host}:{ports} 可达\n", flush=True)
    if args.connect_only:
        return

    levels, results = [], []
    c = args.start_c
    while c <= args.max_c:
        per_worker = c // args.workers
        print(f"--- 级别 {len(levels)+1}: 总并发 {per_worker*args.workers} "
              f"({args.workers} worker x {per_worker} 连接, 均分 {len(ports)} 端口) ---", flush=True)
        tpaths, procs = [], []
        for w in range(args.workers):
            tp = os.path.join(tempfile.gettempdir(), f"ab_w{w}_{c}_{os.getpid()}.json")
            tpaths.append(tp)
            procs.append(subprocess.Popen(
                [sys.executable, __file__, '--role', 'worker', '--host', args.host,
                 '--ports', ','.join(map(str, ports)), '--level-c', str(c),
                 '--duration', str(args.duration), '--worker-id', str(w),
                 '--total-workers', str(args.workers), '--out', tp],
                stdout=sys.stdout))
        try:
            for p in procs: p.wait(timeout=args.duration + 120)
        except subprocess.TimeoutExpired:
            for p in procs: p.kill()
            print("  !! 子进程超时, 终止本级", flush=True)

        agg = aggregate(tpaths, per_worker * args.workers)
        for tp in tpaths:
            try: os.unlink(tp)
            except OSError: pass
        levels.append(c)
        results.append(agg)
        err_rate = agg["errors"] / max(agg["requests"] + agg["errors"], 1)
        conn_fail_rate = agg["connect_failed"] / max(c, 1)
        print(f"  >>> 总 QPS {agg['qps']:>10,.0f} | P50 {agg['p50']:6.1f}ms "
              f"P99 {agg['p99']:7.1f}ms | 错误率 {err_rate*100:.2f}% "
              f"建连失败 {conn_fail_rate*100:.1f}%\n", flush=True)

        if agg["qps"] == 0:
            print("!! 本级零吞吐, 终止", flush=True); break
        if err_rate > 0.01 or conn_fail_rate > 0.10:
            print(">> 错误率/建连失败超阈值, 判定过载, 收口", flush=True); break
        if len(results) >= 2 and agg["qps"] < results[-2]["qps"] * (1 + args.gain_stop):
            print(">> QPS 进入平台期(提升 < {:.0%}), 饱和点可能在: 客户端CPU/公网RTT/公网带宽/服务端, 结合服务端 CPU 判定".format(args.gain_stop), flush=True)
            break
        c *= 2
        if c > args.max_c:
            print(">> 达到 --max-c 上限, 收口", flush=True)

    best = max(results, key=lambda r: r["qps"]) if results else None
    print("\n" + "=" * 64)
    print(f"{'并发':>8} {'QPS':>12} {'P50(ms)':>9} {'P99(ms)':>9} {'错误率':>8}")
    for lv, r in zip(levels, results):
        er = r["errors"] / max(r["requests"] + r["errors"], 1)
        print(f"{lv:>8} {r['qps']:>12,.0f} {r['p50']:>9.1f} {r['p99']:>9.1f} {er*100:>7.2f}%")
    if best:
        print("=" * 64)
        print(f"峰值: {best['qps']:,.0f} QPS @ 并发 {best['concurrency']} "
              f"(P50 {best['p50']:.1f}ms / P99 {best['p99']:.1f}ms)")
        out = os.path.join(os.getcwd(), f"autobench_{time.strftime('%H%M%S')}.json")
        json.dump({"host": args.host, "ports": ports, "levels": dict(zip(map(str, levels), results)),
                   "peak": best}, open(out, 'w'), indent=2)
        print(f"结果已保存: {out}")

if __name__ == '__main__':
    main()
