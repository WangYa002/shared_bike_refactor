#!/usr/bin/env python3
"""
shared_bike 压测客户端 — 纯 Python, 零依赖 (只用 asyncio + struct)

两种模式:
  rtt  : N 个并发, 每个开新连接做 1 次完整登录链路 (mobile_code -> login),
         测端到端 RTT 分布 (P50/P90/P95/P99).
  qps  : N 条长连接, 每条反复发 mobile_code 请求 (有响应), 持续 D 秒,
         测网络层 + 业务层吞吐与平均延迟.

协议 (与 common/include/bike/protocol.hpp 一致):
  +4 bytes magic "FBEB"
  +2 bytes event_id (u16 LE)
  +4 bytes length   (i32 LE)
  +N bytes payload  (serialized protobuf)

用法示例:
  python bike_bench.py rtt -c 200
  python bike_bench.py qps -c 200 -d 30
"""
import argparse
import asyncio
import json
import struct
import sys
import time
import statistics

MAGIC = b'FBEB'
HEADER_LEN = 10
MAX_MSG_LEN = 372680

# ----------------- minimal protobuf encoding -----------------

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

def encode_tag(field: int, wire_type: int) -> bytes:
    return encode_varint((field << 3) | wire_type)

def encode_string(field: int, s: str) -> bytes:
    b = s.encode('utf-8')
    return encode_tag(field, 2) + encode_varint(len(b)) + b

def encode_int32(field: int, v: int) -> bytes:
    return encode_tag(field, 0) + encode_varint(v)

# messages we need
def encode_mobile_request(mobile: str) -> bytes:
    return encode_string(1, mobile)

def encode_login_request(mobile: str, icode: int) -> bytes:
    return encode_string(1, mobile) + encode_int32(2, icode)

# ----------------- frame encode/decode -----------------

def encode_frame(eid: int, payload: bytes) -> bytes:
    return MAGIC + struct.pack('<Hi', eid, len(payload)) + payload

async def read_frame(reader):
    hdr = await reader.readexactly(HEADER_LEN)
    if hdr[:4] != MAGIC:
        raise ValueError(f"bad magic: {hdr[:4]!r}")
    eid, = struct.unpack('<H', hdr[4:6])
    length, = struct.unpack('<i', hdr[6:10])
    if length < 0 or length > MAX_MSG_LEN:
        raise ValueError(f"bad length: {length}")
    body = await reader.readexactly(length) if length > 0 else b''
    return eid, body

# ----------------- minimal protobuf decoding -----------------

def decode_varint(buf: bytes, pos: int):
    val = 0; shift = 0
    while True:
        b = buf[pos]; pos += 1
        val |= (b & 0x7F) << shift
        if not (b & 0x80):
            return val & 0xFFFFFFFF, pos
        shift += 7

def decode_mobile_response(buf: bytes):
    """mobile_response { int32 code=1; int32 icode=2; string data=3; }"""
    pos = 0; code = 0; icode = 0
    while pos < len(buf):
        tag, pos = decode_varint(buf, pos)
        field = tag >> 3; wt = tag & 7
        if wt == 0:
            v, pos = decode_varint(buf, pos)
            if field == 1: code = v
            elif field == 2: icode = v
        elif wt == 2:
            ln, pos = decode_varint(buf, pos)
            pos += ln
    return code, icode

def decode_login_response(buf: bytes):
    """login_response { int32 code=1; string desc=2; string session_token=3; }"""
    pos = 0; code = 0; token = ''
    while pos < len(buf):
        tag, pos = decode_varint(buf, pos)
        field = tag >> 3; wt = tag & 7
        if wt == 0:
            v, pos = decode_varint(buf, pos)
            if field == 1: code = v
        elif wt == 2:
            ln, pos = decode_varint(buf, pos)
            data = buf[pos:pos+ln]; pos += ln
            if field == 3: token = data.decode('utf-8', 'replace')
    return code, token

# ----------------- bench scenarios -----------------

def make_mobile(prefix: str, worker_id: int, total_workers: int, i: int) -> str:
    """Partition mobile-number space so concurrent workers don't collide on Redis keys.

    Each worker owns a 100_000-number block: worker N gets indices
    [N*100_000, (N+1)*100_000). Connection i within the worker maps to
    index N*100_000 + i.
    """
    if total_workers <= 0:
        total_workers = 1
    if worker_id < 0 or worker_id >= total_workers:
        raise ValueError(f"worker_id {worker_id} out of range [0, {total_workers})")
    idx = worker_id * 100_000 + i
    return f"{prefix}{idx:08d}"


async def do_login_round(host: str, port: int, mobile: str) -> float:
    """完整登录链路: mobile_code -> login. 返回链路 RTT(ms). 失败抛异常."""
    t0 = time.perf_counter()
    reader, writer = await asyncio.open_connection(host, port)
    try:
        # 1. mobile_code
        writer.write(encode_frame(0x01, encode_mobile_request(mobile)))
        await writer.drain()
        eid, body = await read_frame(reader)
        code, icode = decode_mobile_response(body)
        if code != 200 or icode == 0:
            raise RuntimeError(f"mobile_code failed: code={code} icode={icode}")
        # 2. login
        writer.write(encode_frame(0x03, encode_login_request(mobile, icode)))
        await writer.drain()
        eid, body = await read_frame(reader)
        lcode, token = decode_login_response(body)
        if lcode != 200 or not token:
            raise RuntimeError(f"login failed: code={lcode}")
        return (time.perf_counter() - t0) * 1000.0
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass


async def bench_rtt(host: str, port: int, concurrency: int,
                    worker_id: int = 0, total_workers: int = 1,
                    stats_out: list = None):
    """每个并发开 1 个新连接, 做 1 次完整登录链路."""
    print(f"=== RTT bench [worker {worker_id}/{total_workers}]: {concurrency} 个并发新连接 ===\n", flush=True)
    latencies = []
    errors = []
    sem = asyncio.Semaphore(concurrency)

    async def one(i: int):
        mobile = make_mobile("138", worker_id, total_workers, i)
        async with sem:
            try:
                rt = await do_login_round(host, port, mobile)
                latencies.append(rt)
            except Exception as e:
                errors.append(str(e))

    t0 = time.perf_counter()
    await asyncio.gather(*(one(i) for i in range(concurrency)))
    elapsed = time.perf_counter() - t0
    stats = _report("RTT-login", latencies, errors, elapsed, concurrency)
    if stats_out is not None:
        stats_out.append(stats)


async def bench_qps(host: str, port: int, concurrency: int, duration: int,
                    worker_id: int = 0, total_workers: int = 1,
                    stats_out: list = None):
    """N 条长连接, 每条持续 D 秒反复发 mobile_code 请求."""
    print(f"=== QPS bench [worker {worker_id}/{total_workers}]: {concurrency} 条长连接, 持续 {duration}s ===\n", flush=True)
    latencies = []
    errors = []
    total_reqs = 0
    stop = asyncio.Event()

    async def worker(i: int):
        nonlocal total_reqs
        mobile = make_mobile("139", worker_id, total_workers, i)
        reader, writer = await asyncio.open_connection(host, port)
        try:
            while not stop.is_set():
                t0 = time.perf_counter()
                try:
                    writer.write(encode_frame(0x01, encode_mobile_request(mobile)))
                    await writer.drain()
                    eid, body = await read_frame(reader)
                    code, icode = decode_mobile_response(body)
                    if code != 200 or icode == 0:
                        errors.append(f"worker{i}: bad code={code}")
                        continue
                    latencies.append((time.perf_counter() - t0) * 1000.0)
                    total_reqs += 1
                except Exception as e:
                    errors.append(f"worker{i}: {e!r}")
                    return
        finally:
            try:
                writer.close()
            except Exception:
                pass

    # ramp up connections first
    workers = []
    t_connect_start = time.perf_counter()
    for i in range(concurrency):
        try:
            w = asyncio.create_task(worker(i))
            workers.append(w)
        except Exception as e:
            errors.append(f"connect {i}: {e!r}")
    connect_dur = time.perf_counter() - t_connect_start
    print(f"  连接建立: {concurrency - sum('connect' in e for e in errors)}/{concurrency} 成功 ({connect_dur:.2f}s)\n", flush=True)

    # run for duration
    await asyncio.sleep(duration)
    stop.set()
    await asyncio.gather(*workers, return_exceptions=True)

    elapsed = duration
    stats = _report("QPS-mobile_code", latencies, errors, elapsed, concurrency)
    if stats_out is not None:
        stats_out.append(stats)


def _compute_stats(name: str, latencies: list, errors: list, elapsed: float, concurrency: int) -> dict:
    n = len(latencies)
    qps = n / elapsed if elapsed > 0 else 0

    def pct(p):
        if not latencies: return 0.0
        s = sorted(latencies)
        idx = min(len(s) - 1, int(len(s) * p / 100))
        return s[idx]

    return {
        "name": name,
        "concurrency": concurrency,
        "duration_sec": elapsed,
        "requests_ok": n,
        "errors": len(errors),
        "qps": qps,
        "latency_ms": {
            "min": min(latencies) if latencies else 0.0,
            "avg": statistics.mean(latencies) if latencies else 0.0,
            "max": max(latencies) if latencies else 0.0,
            "p50": pct(50), "p90": pct(90), "p95": pct(95), "p99": pct(99),
        },
        "samples": list(latencies),
    }


def _report(name: str, latencies: list, errors: list, elapsed: float, concurrency: int) -> dict:
    stats = _compute_stats(name, latencies, errors, elapsed, concurrency)
    n = stats["requests_ok"]
    qps = stats["qps"]
    print(f"--- {name} ---")
    print(f"  concurrency  : {concurrency}")
    print(f"  duration     : {elapsed:.2f}s")
    print(f"  requests ok  : {n}")
    print(f"  errors       : {len(errors)}")
    if errors:
        uniq = {}
        for e in errors:
            key = e.split(':')[0] if ':' in e else e
            uniq[key] = uniq.get(key, 0) + 1
        for k, v in sorted(uniq.items(), key=lambda x: -x[1])[:5]:
            print(f"    [{v:>4}] {k}")
    print(f"  QPS          : {qps:.1f}")
    if n > 0:
        lm = stats["latency_ms"]
        print(f"  latency(ms)  : min={lm['min']:.2f}  avg={lm['avg']:.2f}  max={lm['max']:.2f}")
        print(f"               : P50={lm['p50']:.2f}  P90={lm['p90']:.2f}  P95={lm['p95']:.2f}  P99={lm['p99']:.2f}")
    print()
    return stats


async def main_async(args):
    stats_out = []
    if args.mode == 'rtt':
        await bench_rtt(args.host, args.port, args.concurrency,
                        args.worker_id, args.total_workers, stats_out)
    else:
        await bench_qps(args.host, args.port, args.concurrency, args.duration,
                        args.worker_id, args.total_workers, stats_out)
    if args.json_out and stats_out:
        stats = dict(stats_out[0])
        stats["worker_id"] = args.worker_id
        stats["total_workers"] = args.total_workers
        stats["host"] = args.host
        stats["port"] = args.port
        with open(args.json_out, 'w') as f:
            json.dump(stats, f)


def main():
    p = argparse.ArgumentParser(description='shared_bike bench client')
    p.add_argument('mode', choices=['rtt', 'qps'])
    p.add_argument('--host', default='127.0.0.1')
    p.add_argument('--port', type=int, default=8888)
    p.add_argument('-c', '--concurrency', type=int, default=100)
    p.add_argument('-d', '--duration', type=int, default=30, help='qps 模式持续秒数')
    p.add_argument('--worker-id', type=int, default=0,
                   help='0-based index of this worker; partitions mobile-number space')
    p.add_argument('--total-workers', type=int, default=1,
                   help='total number of concurrent worker processes')
    p.add_argument('--json-out', default=None,
                   help='path to write final stats as a single JSON object')
    args = p.parse_args()

    if args.worker_id < 0 or args.worker_id >= args.total_workers:
        p.error(f"--worker-id must be in [0, {args.total_workers}); got {args.worker_id}")

    if sys.platform == 'win32':
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

    try:
        asyncio.run(main_async(args))
    except KeyboardInterrupt:
        print("\n中断.")

if __name__ == '__main__':
    main()
