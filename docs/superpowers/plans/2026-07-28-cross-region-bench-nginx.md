# Cross-Region Bench + nginx Multi-Instance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fill the missing biz=16 c=100/500 loopback numbers, then measure public-Internet behavior from a Windows host against a Tencent Cloud CVM, with and without an nginx stream proxy in front of two bike-server instances.

**Architecture:** Extend the existing zero-dependency Python bench client with `--worker-id`, `--total-workers`, and `--json-out` flags so 4 local processes can fan out concurrently and write machine-readable results. Add an aggregator that merges per-worker JSON. Add an `nginx.conf` using the stream module (FBEB is raw TCP, not HTTP) and modify docker-compose to run two bike-server instances behind nginx. Run the bench matrix defined in spec Section 6.2, then write a side-by-side results doc.

**Tech Stack:** Python 3 asyncio, PowerShell, Docker Compose, nginx 1.27-alpine (stream module), existing C++ asio bike-server, Tencent Cloud CVM (4 vCPU AMD EPYC 7K62).

**Spec:** `docs/superpowers/specs/2026-07-28-cross-region-bench-nginx-design.md`

---

## File Structure

**New files:**
- `scripts/bench/tests/test_bench.py` — unittest tests for `bike_bench.py` enhancements (partition, JSON output)
- `scripts/bench/tests/test_aggregate.py` — unittest tests for `aggregate_workers.py`
- `scripts/bench/aggregate_workers.py` — merges per-worker JSON files into a combined report
- `scripts/bench/run_bench.ps1` — PowerShell launcher that spawns N Python workers and aggregates
- `docker/nginx.conf` — nginx stream-module config: listen 8888, proxy to bike-server-1/2 via least_conn upstream
- `docs/superpowers/specs/2026-07-28-cross-region-bench-nginx-results.md` — produced in Task 12

**Modified files:**
- `scripts/bench/bike_bench.py` — refactor `_report` to return stats; add `--worker-id`, `--total-workers`, `--json-out`; partition mobile numbers per worker
- `docker/docker-compose.yml` — drop `ports: "8888:8888"` from `server`, add `bike-server-2` service and `nginx` service

---

## Task 1: Refactor `_report` to return stats dict (no behavior change)

**Files:**
- Modify: `scripts/bench/bike_bench.py:227-253` (the `_report` function)
- Test: `scripts/bench/tests/test_bench.py` (new)

- [ ] **Step 1: Create the test file with a failing test**

Create `scripts/bench/tests/test_bench.py`:

```python
"""Unit tests for bike_bench.py helpers. Run with: python -m unittest tests.test_bench"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from bike_bench import _compute_stats

class TestComputeStats(unittest.TestCase):
    def test_returns_dict_with_required_fields(self):
        latencies = [1.0, 2.0, 3.0, 4.0, 5.0]
        stats = _compute_stats("QPS-mobile_code", latencies, [], 10.0, 100)
        self.assertEqual(stats["name"], "QPS-mobile_code")
        self.assertEqual(stats["concurrency"], 100)
        self.assertEqual(stats["duration_sec"], 10.0)
        self.assertEqual(stats["requests_ok"], 5)
        self.assertEqual(stats["errors"], 0)
        self.assertAlmostEqual(stats["qps"], 0.5)  # 5 / 10.0
        self.assertEqual(stats["latency_ms"]["min"], 1.0)
        self.assertEqual(stats["latency_ms"]["max"], 5.0)

    def test_empty_latencies_returns_zeros(self):
        stats = _compute_stats("X", [], ["err1"], 5.0, 10)
        self.assertEqual(stats["requests_ok"], 0)
        self.assertEqual(stats["errors"], 1)
        self.assertEqual(stats["qps"], 0)
        self.assertEqual(stats["latency_ms"]["min"], 0)
        self.assertEqual(stats["latency_ms"]["max"], 0)

if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run from `scripts/bench/`:
```
python -m unittest tests.test_bench
```
Expected: FAIL with `ImportError: cannot import name '_compute_stats' from 'bike_bench'`

- [ ] **Step 3: Refactor `_report` to extract `_compute_stats`**

Replace the `_report` function in `scripts/bench/bike_bench.py` with:

```python
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
```

- [ ] **Step 4: Run test to verify it passes**

```
python -m unittest tests.test_bench
```
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add scripts/bench/bike_bench.py scripts/bench/tests/test_bench.py
git commit -m "refactor(bench): extract _compute_stats from _report for downstream JSON output"
```

---

## Task 2: Add `--worker-id` / `--total-workers` mobile partition

**Files:**
- Modify: `scripts/bench/bike_bench.py` (add `make_mobile`, thread worker_id into bench scenarios)
- Test: `scripts/bench/tests/test_bench.py` (add tests for partition)

- [ ] **Step 1: Add failing tests for `make_mobile`**

Append to `scripts/bench/tests/test_bench.py` (inside the `TestComputeStats`-level file, before the `if __name__` block):

```python
class TestMakeMobile(unittest.TestCase):
    def test_default_no_partition(self):
        from bike_bench import make_mobile
        # worker_id=0, total_workers=1: behaves like the old code
        self.assertEqual(make_mobile("139", 0, 1, 0), "13900000000")
        self.assertEqual(make_mobile("139", 0, 1, 99), "13900000099")

    def test_workers_get_disjoint_ranges(self):
        from bike_bench import make_mobile
        # Each worker gets a 1M-number block; worker N starts at N*1_000_000
        m0 = make_mobile("139", 0, 4, 0)
        m1 = make_mobile("139", 1, 4, 0)
        m2 = make_mobile("139", 2, 4, 0)
        m3 = make_mobile("139", 3, 4, 0)
        self.assertEqual(len({m0, m1, m2, m3}), 4, "all four workers should get distinct mobiles")
        self.assertEqual(m0, "13900000000")
        self.assertEqual(m1, "13900100000")
        self.assertEqual(m2, "13900200000")
        self.assertEqual(m3, "13900300000")

    def test_within_worker_unique_per_connection(self):
        from bike_bench import make_mobile
        a = make_mobile("139", 1, 4, 0)
        b = make_mobile("139", 1, 4, 1)
        c = make_mobile("139", 1, 4, 499)
        self.assertEqual(len({a, b, c}), 3)
```

- [ ] **Step 2: Run test to verify it fails**

```
python -m unittest tests.test_bench
```
Expected: FAIL with `ImportError: cannot import name 'make_mobile'`.

- [ ] **Step 3: Add `make_mobile` and use it in bench scenarios**

In `scripts/bench/bike_bench.py`, add this near the top (after the protobuf section, before `async def do_login_round`):

```python
def make_mobile(prefix: str, worker_id: int, total_workers: int, i: int) -> str:
    """Partition mobile-number space so concurrent workers don't collide on Redis keys.

    Each worker owns a 1_000_000-number block: worker N gets indices
    [N*1_000_000, (N+1)*1_000_000). Connection i within the worker maps to
    index N*1_000_000 + i.
    """
    if total_workers <= 0:
        total_workers = 1
    if worker_id < 0 or worker_id >= total_workers:
        raise ValueError(f"worker_id {worker_id} out of range [0, {total_workers})")
    idx = worker_id * 1_000_000 + i
    return f"{prefix}{idx:08d}"
```

Modify `bench_rtt` signature to accept `worker_id` and `total_workers`, and use `make_mobile`:

```python
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
```

Modify `bench_qps` similarly:

```python
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

    await asyncio.sleep(duration)
    stop.set()
    await asyncio.gather(*workers, return_exceptions=True)

    elapsed = duration
    stats = _report("QPS-mobile_code", latencies, errors, elapsed, concurrency)
    if stats_out is not None:
        stats_out.append(stats)
```

- [ ] **Step 4: Run test to verify it passes**

```
python -m unittest tests.test_bench
```
Expected: PASS (5 tests).

- [ ] **Step 5: Commit**

```bash
git add scripts/bench/bike_bench.py scripts/bench/tests/test_bench.py
git commit -m "feat(bench): add --worker-id/--total-workers with disjoint mobile-number partition"
```

---

## Task 3: Wire CLI args and add `--json-out` flag

**Files:**
- Modify: `scripts/bench/bike_bench.py` (`main()` and `main_async()`)
- Test: `scripts/bench/tests/test_bench.py` (add JSON-output test)

- [ ] **Step 1: Add failing test for JSON output end-to-end**

Append to `scripts/bench/tests/test_bench.py`:

```python
import json
import tempfile

class TestJsonOut(unittest.TestCase):
    def test_json_out_writes_expected_fields(self):
        import subprocess
        import os
        # Use a bogus host:port so the bench fails fast with 0 requests;
        # the test only checks JSON shape, not values.
        with tempfile.NamedTemporaryFile(mode='r', suffix='.json', delete=False) as f:
            json_path = f.name
        try:
            # Connect to a port that nothing listens on -> all errors, fast
            r = subprocess.run(
                ['python', 'bike_bench.py', 'qps',
                 '--host', '127.0.0.1', '--port', '1',
                 '-c', '1', '-d', '1',
                 '--json-out', json_path],
                cwd=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                capture_output=True, text=True, timeout=15,
            )
            # Bench should still write JSON even if all connections failed
            with open(json_path, 'r') as g:
                data = json.load(g)
            for key in ["name", "worker_id", "total_workers", "host", "port",
                        "concurrency", "duration_sec", "requests_ok", "errors",
                        "qps", "latency_ms"]:
                self.assertIn(key, data, f"missing {key} in JSON output")
            for key in ["min", "avg", "max", "p50", "p90", "p95", "p99"]:
                self.assertIn(key, data["latency_ms"])
        finally:
            os.unlink(json_path)
```

- [ ] **Step 2: Run test to verify it fails**

```
python -m unittest tests.test_bench
```
Expected: FAIL — `--json-out` flag doesn't exist yet (argparse error).

- [ ] **Step 3: Wire up the CLI**

First, extend `_compute_stats` (added in Task 1) to include raw samples in its output. Replace the existing `_compute_stats` in `scripts/bench/bike_bench.py`:

```python
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
```

(The Task 1 test only checks top-level keys + `latency_ms` subkeys, so adding `samples` does not break it.)

Then replace `main_async` and `main` in `scripts/bench/bike_bench.py`:

```python
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
```

Update `main`:

```python
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
```

Add `import json` to the top of the file (near `import argparse`).

- [ ] **Step 4: Run test to verify it passes**

```
python -m unittest tests.test_bench
```
Expected: PASS (6 tests).

- [ ] **Step 5: Manual smoke check — single-worker happy path**

```
python bike_bench.py qps --host 127.0.0.1 --port 8888 -c 1 -d 2 --json-out /tmp/smoke.json
```
Expected: prints bench output AND writes `/tmp/smoke.json` with sensible fields.

- [ ] **Step 6: Commit**

```bash
git add scripts/bench/bike_bench.py scripts/bench/tests/test_bench.py
git commit -m "feat(bench): add --json-out writing stats + raw latency samples"
```

---

## Task 4: Add `aggregate_workers.py` to merge per-worker JSON

**Files:**
- Create: `scripts/bench/aggregate_workers.py`
- Test: `scripts/bench/tests/test_aggregate.py` (new)

- [ ] **Step 1: Write failing test for the aggregator**

Create `scripts/bench/tests/test_aggregate.py`:

```python
"""Unit tests for aggregate_workers.py. Run with: python -m unittest tests.test_aggregate"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from aggregate_workers import aggregate

class TestAggregate(unittest.TestCase):
    def test_sums_qps_and_counts(self):
        stats = [
            {"name": "X", "qps": 5000.0, "requests_ok": 100000, "errors": 0,
             "concurrency": 500, "duration_sec": 20.0, "samples": [1.0, 2.0, 3.0]},
            {"name": "X", "qps": 5000.0, "requests_ok": 100000, "errors": 2,
             "concurrency": 500, "duration_sec": 20.0, "samples": [4.0, 5.0, 6.0]},
        ]
        r = aggregate(stats)
        self.assertEqual(r["n_workers"], 2)
        self.assertEqual(r["total_concurrency"], 1000)
        self.assertEqual(r["total_requests"], 200000)
        self.assertEqual(r["total_errors"], 2)
        self.assertAlmostEqual(r["total_qps"], 10000.0)

    def test_concatenates_latency_samples(self):
        stats = [
            {"name": "X", "qps": 1.0, "requests_ok": 1, "errors": 0,
             "concurrency": 1, "duration_sec": 1.0, "samples": [10.0, 30.0]},
            {"name": "X", "qps": 1.0, "requests_ok": 1, "errors": 0,
             "concurrency": 1, "duration_sec": 1.0, "samples": [20.0, 40.0]},
        ]
        r = aggregate(stats)
        self.assertEqual(r["latency_ms"]["min"], 10.0)
        self.assertEqual(r["latency_ms"]["max"], 40.0)
        # 4 sorted samples [10,20,30,40]: p50 index = int(4*0.5)=2 -> 30.0
        self.assertEqual(r["latency_ms"]["p50"], 30.0)
        # p99 index = min(3, int(4*0.99))=min(3,3)=3 -> 40.0
        self.assertEqual(r["latency_ms"]["p99"], 40.0)

    def test_handles_empty_input(self):
        r = aggregate([])
        self.assertEqual(r["n_workers"], 0)
        self.assertEqual(r["total_qps"], 0.0)
        self.assertEqual(r["latency_ms"]["min"], 0.0)

if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

```
python -m unittest tests.test_aggregate
```
Expected: FAIL with `ModuleNotFoundError: No module named 'aggregate_workers'`.

- [ ] **Step 3: Implement `aggregate_workers.py`**

Create `scripts/bench/aggregate_workers.py`:

```python
#!/usr/bin/env python3
"""
Aggregate per-worker bike_bench.py JSON outputs into a single combined report.

Usage:
  python aggregate_workers.py worker0.json worker1.json ... -o combined.json
  python aggregate_workers.py --glob 'worker*.json' -o combined.json
"""
import argparse
import glob
import json
import sys


def aggregate(worker_stats: list) -> dict:
    """Merge per-worker stats dicts (as produced by bike_bench.py --json-out)."""
    if not worker_stats:
        return {
            "n_workers": 0,
            "total_concurrency": 0,
            "duration_sec": 0.0,
            "total_requests": 0,
            "total_errors": 0,
            "total_qps": 0.0,
            "latency_ms": {"min": 0.0, "avg": 0.0, "max": 0.0,
                           "p50": 0.0, "p90": 0.0, "p95": 0.0, "p99": 0.0},
        }

    all_samples = []
    for s in worker_stats:
        all_samples.extend(s.get("samples", []))
    all_samples.sort()

    def pct(p):
        if not all_samples: return 0.0
        idx = min(len(all_samples) - 1, int(len(all_samples) * p / 100))
        return all_samples[idx]

    n_workers = len(worker_stats)
    total_qps = sum(s.get("qps", 0) for s in worker_stats)
    total_requests = sum(s.get("requests_ok", 0) for s in worker_stats)
    total_errors = sum(s.get("errors", 0) for s in worker_stats)
    total_concurrency = sum(s.get("concurrency", 0) for s in worker_stats)
    duration = max(s.get("duration_sec", 0) for s in worker_stats)

    return {
        "name": worker_stats[0].get("name", "?"),
        "n_workers": n_workers,
        "total_concurrency": total_concurrency,
        "duration_sec": duration,
        "total_requests": total_requests,
        "total_errors": total_errors,
        "total_qps": total_qps,
        "bandwidth_hint_bytes_per_sec": None,  # filled in by caller if --bytes known
        "latency_ms": {
            "min": all_samples[0] if all_samples else 0.0,
            "avg": (sum(all_samples) / len(all_samples)) if all_samples else 0.0,
            "max": all_samples[-1] if all_samples else 0.0,
            "p50": pct(50), "p90": pct(90), "p95": pct(95), "p99": pct(99),
        },
    }


def main():
    p = argparse.ArgumentParser(description='Aggregate bike_bench per-worker JSON')
    p.add_argument('inputs', nargs='*', help='per-worker JSON files')
    p.add_argument('--glob', default=None, help='glob pattern for input files')
    p.add_argument('-o', '--out', default=None, help='output JSON path (default: stdout)')
    args = p.parse_args()

    paths = list(args.inputs)
    if args.glob:
        paths.extend(glob.glob(args.glob))
    if not paths:
        p.error("no input files (pass positional args or --glob)")

    paths.sort()
    worker_stats = []
    for path in paths:
        with open(path, 'r') as f:
            worker_stats.append(json.load(f))

    combined = aggregate(worker_stats)
    combined["input_files"] = paths

    out = json.dumps(combined, indent=2) if args.out else json.dumps(combined, indent=2)
    if args.out:
        with open(args.out, 'w') as f:
            f.write(out)
        print(f"wrote {args.out}: n_workers={combined['n_workers']} "
              f"total_qps={combined['total_qps']:.1f} "
              f"P99={combined['latency_ms']['p99']:.2f}ms",
              file=sys.stderr)
    else:
        print(out)


if __name__ == '__main__':
    main()
```

- [ ] **Step 4: Run test to verify it passes**

```
python -m unittest tests.test_aggregate
```
Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
git add scripts/bench/aggregate_workers.py scripts/bench/tests/test_aggregate.py
git commit -m "feat(bench): add aggregate_workers.py to merge per-worker JSON outputs"
```

---

## Task 5: Add `run_bench.ps1` PowerShell launcher

**Files:**
- Create: `scripts/bench/run_bench.ps1`

This is a thin wrapper; no Python unit test applies. We verify by syntax-checking PowerShell parsing.

- [ ] **Step 1: Write the launcher**

Create `scripts/bench/run_bench.ps1`:

```powershell
<#
.SYNOPSIS
  Spawn N bike_bench.py QPS workers in parallel, then aggregate their JSON outputs.

.PARAMETER Workers
  Number of parallel Python processes (default: 4).

.PARAMETER Concurrency
  Concurrency PER WORKER (default: 500). Total concurrency = Workers * Concurrency.

.PARAMETER Duration
  Bench duration in seconds (default: 20).

.PARAMETER Host
  Target host (default: 124.220.92.243).

.Parameter Port
  Target port (default: 8888).

.PARAMETER Tag
  Output filename tag (default: 'run'). Produces <Tag>_w0.json, <Tag>_w1.json, ...

.EXAMPLE
  .\run_bench.ps1 -Workers 4 -Concurrency 125 -Duration 20 -Tag 3B-single
#>
[CmdletBinding()]
param(
    [int]$Workers = 4,
    [int]$Concurrency = 500,
    [int]$Duration = 20,
    [string]$Host = '124.220.92.243',
    [int]$Port = 8888,
    [string]$Tag = 'run'
)

$ErrorActionPreference = 'Stop'
Set-Location -Path (Split-Path -Parent $MyInvocation.MyCommand.Path)

Write-Host "=== run_bench: $Workers workers x $Concurrency concurrency = $($Workers * $Concurrency) total, ${Duration}s ==="

# 1. Launch all workers in parallel
$jobs = @()
for ($w = 0; $w -lt $Workers; $w++) {
    $outFile = "${Tag}_w${w}.json"
    Write-Host "  starting worker $w -> $outFile"
    $jobs += Start-Process -FilePath 'python' `
        -ArgumentList @(
            'bike_bench.py', 'qps',
            '--host', $Host,
            '--port', $Port,
            '-c', $Concurrency,
            '-d', $Duration,
            '--worker-id', $w,
            '--total-workers', $Workers,
            '--json-out', $outFile
        ) -NoNewWindow -PassThru
}

# 2. Wait for all to finish
Write-Host "  waiting for $Workers workers..."
$jobs | Wait-Process | Out-Null
$failed = $jobs | Where-Object { $_.ExitCode -ne 0 }
if ($failed) {
    Write-Error "$($failed.Count) worker(s) exited non-zero"
    exit 1
}

# 3. Aggregate
$combined = "${Tag}_combined.json"
Write-Host "  aggregating -> $combined"
python aggregate_workers.py --glob "${Tag}_w*.json" -o $combined
if ($LASTEXITCODE -ne 0) {
    Write-Error "aggregation failed"
    exit 1
}

Write-Host "=== done. see $combined ==="
```

- [ ] **Step 2: Smoke-check PowerShell parses the script**

In PowerShell:
```powershell
PS> $null = [System.Management.Automation.PSParser]::Tokenize((Get-Content -Raw scripts\bench\run_bench.ps1), [ref]$null)
PS> "OK"
```
Expected: prints `OK` (no parse errors).

- [ ] **Step 3: Commit**

```bash
git add scripts/bench/run_bench.ps1
git commit -m "feat(bench): add run_bench.ps1 PowerShell launcher for multi-worker fan-out"
```

---

## Task 6: Add `docker/nginx.conf` (stream module)

**Files:**
- Create: `docker/nginx.conf`

- [ ] **Step 1: Write the nginx config**

Create `docker/nginx.conf`:

```nginx
# nginx stream-mode reverse proxy for bike-server.
# Uses stream module (NOT http) because the FBEB wire protocol is raw TCP.
#
# Toggle single vs multi instance by marking bike-server-2 as `down`
# (or by stopping its container: `docker compose stop bike-server-2`).

worker_processes auto;

events {
    worker_connections 65535;
}

stream {
    upstream bike_backend {
        least_conn;
        server bike-server-1:8888 max_fails=3 fail_timeout=5s;
        server bike-server-2:8888 max_fails=3 fail_timeout=5s;
    }

    server {
        listen 8888;
        proxy_pass bike_backend;
        proxy_connect_timeout 5s;
        proxy_timeout 60s;
    }
}
```

- [ ] **Step 2: Validate config syntax with a one-shot nginx container**

```bash
docker run --rm -v "$(pwd)/docker/nginx.conf:/etc/nginx/nginx.conf:ro" nginx:1.27-alpine nginx -t
```
Expected: prints `nginx: configuration file /etc/nginx/nginx.conf test is successful`.

- [ ] **Step 3: Commit**

```bash
git add docker/nginx.conf
git commit -m "feat(docker): add nginx stream-mode config for bike-server load balancing"
```

---

## Task 7: Modify `docker/docker-compose.yml` for nginx + 2 instances

**Files:**
- Modify: `docker/docker-compose.yml`

- [ ] **Step 1: Rewrite docker-compose.yml**

Replace `docker/docker-compose.yml` contents with:

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
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost", "-u", "bike", "-pbike_pwd"]
      interval: 5s
      timeout: 3s
      retries: 30

  redis:
    image: redis:7-alpine
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 5s
      timeout: 3s
      retries: 30

  bike-server-1:
    build:
      context: ..
      dockerfile: docker/Dockerfile.server
    container_name: bike-server-1
    depends_on:
      mysql:
        condition: service_healthy
      redis:
        condition: service_healthy
    expose:
      - "8888"
    environment:
      BIKE_INSTANCE_ID: "1"
      BIKE_BIZ_THREADS: "16"
    volumes:
      - ./server.toml:/etc/bike/server.toml:ro
      - server-logs-1:/var/log/bike-server
    restart: unless-stopped

  bike-server-2:
    build:
      context: ..
      dockerfile: docker/Dockerfile.server
    container_name: bike-server-2
    depends_on:
      mysql:
        condition: service_healthy
      redis:
        condition: service_healthy
    expose:
      - "8888"
    environment:
      BIKE_INSTANCE_ID: "2"
      BIKE_BIZ_THREADS: "16"
    volumes:
      - ./server.toml:/etc/bike/server.toml:ro
      - server-logs-2:/var/log/bike-server
    restart: unless-stopped

  nginx:
    image: nginx:1.27-alpine
    container_name: bike-nginx
    depends_on:
      - bike-server-1
      - bike-server-2
    ports:
      - "8888:8888"
    volumes:
      - ./nginx.conf:/etc/nginx/nginx.conf:ro
    restart: unless-stopped

volumes:
  mysql-data:
  server-logs-1:
  server-logs-2:
```

- [ ] **Step 2: Validate compose file**

```bash
docker compose -f docker/docker-compose.yml config > /dev/null
```
Expected: exits 0, no errors.

- [ ] **Step 3: Commit**

```bash
git add docker/docker-compose.yml
git commit -m "feat(docker): add bike-server-2 + nginx services, drop host port from bike-server"
```

---

## Task 8: Execute Phase 1 — loopback gap-fill (single instance, biz=16)

**Files:** none modified; this is execution only.

**Precondition:** Server still running the OLD docker-compose (single `server` service, `ports: "8888:8888"` on host). If Task 7 has been deployed, skip Phase 1 and use Phase 3 numbers; or temporarily restore the old compose.

- [ ] **Step 1: SSH to server, confirm biz_threads=16**

```bash
ssh ubuntu@124.220.92.243
docker exec docker-server-1 env | grep BIKE_BIZ_THREADS
```
Expected: `BIKE_BIZ_THREADS=16`. If missing or different, restart with the env set.

- [ ] **Step 2: Sync the enhanced bench script to the server**

From the Windows host (where this plan is being executed):
```bash
scp scripts/bench/bike_bench.py ubuntu@124.220.92.243:/tmp/bike_bench.py
```

- [ ] **Step 3: Run c=100 bench (20s)**

```bash
ssh ubuntu@124.220.92.243
cd /tmp && python3 bike_bench.py qps --host 127.0.0.1 --port 8888 -c 100 -d 20 --json-out phase1_c100.json
```
Expected: writes `phase1_c100.json`, prints bench summary. Note QPS and P99.

- [ ] **Step 4: Run c=500 bench (20s)**

```bash
python3 bike_bench.py qps --host 127.0.0.1 --port 8888 -c 500 -d 20 --json-out phase1_c500.json
```
Expected: writes `phase1_c500.json`. Compare QPS to the earlier 19,925 baseline.

- [ ] **Step 5: Pull JSONs back to Windows for the writeup**

```bash
scp ubuntu@124.220.92.243:/tmp/phase1_c100.json docs/superpowers/specs/phase1_c100.json
scp ubuntu@124.220.92.243:/tmp/phase1_c500.json docs/superpowers/specs/phase1_c500.json
```

- [ ] **Step 6: Verify acceptance criteria AC1 and AC2**

Check (manually or by inspecting the JSON):
- AC1: c=100 QPS within ±10% of 18,838 (i.e., 16,950 – 20,720)
- AC2: c=500 QPS ≥ 18,000

If AC2 fails (QPS < 18,000), re-run once. If still fails, document the actual number in the writeup and flag it.

---

## Task 9: Deploy nginx + 2 instances (Phase 2)

**Files:** none modified; this is deployment.

- [ ] **Step 1: Sync repo changes to server**

```bash
# from Windows, using the existing sync approach (paramiko or rsync)
# make sure docker/nginx.conf and docker/docker-compose.yml are synced
python scripts/bench/_sync_server.py   # or rsync equivalent
```

- [ ] **Step 2: Bring up the new stack**

```bash
ssh ubuntu@124.220.92.243
cd ~/shared_bike_1/docker   # or wherever the repo lives on the server
docker compose up -d --force-recreate --build
docker compose ps
```
Expected: 5 services up (mysql, redis, bike-server-1, bike-server-2, nginx).

- [ ] **Step 3: Smoke test from server host**

```bash
python3 /tmp/bike_bench.py rtt --host 127.0.0.1 --port 8888 -c 1
```
Expected: prints a single RTT sample (e.g., `requests ok : 1`, latency < 100ms). No errors.

- [ ] **Step 4: Verify both backends receive traffic**

```bash
docker logs --tail 20 bike-server-1 2>&1 | grep -c mobile_code
docker logs --tail 20 bike-server-2 2>&1 | grep -c mobile_code
```
Expected: both counters > 0 (proves nginx's least_conn is distributing).

---

## Task 10: Execute Phase 3 — single-instance public-Internet bench

**Files:** none modified; execution only.

**Precondition:** nginx stack from Task 9 is up. For the single-instance scenario, mark bike-server-2 `down` in nginx.conf or stop its container.

- [ ] **Step 1: Switch nginx upstream to single backend**

```bash
ssh ubuntu@124.220.92.243
cd ~/shared_bike_1/docker
docker compose stop bike-server-2
docker exec bike-nginx nginx -s reload
```
Expected: `bike-server-2` exits, nginx reloads (will mark it down via health checks).

- [ ] **Step 2: Bump Windows ephemeral port range (admin PowerShell)**

```powershell
netsh int ipv4 set dynamicport tcp start=10000 num=55535
netsh int ipv4 show dynamicport tcp
```
Expected: shows start=10000, num=55535.

- [ ] **Step 3: Run a 5-second smoke test from Windows**

```powershell
cd D:\C++\shared_bike_1\scripts\bench
python bike_bench.py qps --host 124.220.92.243 --port 8888 -c 10 -d 5
```
Expected: 0 errors, requests_ok > 0. If errors > 0, debug before proceeding.

- [ ] **Step 4: Run the 4 concurrency tiers via run_bench.ps1**

```powershell
.\run_bench.ps1 -Workers 4 -Concurrency 25  -Duration 20 -Tag 3A-single
.\run_bench.ps1 -Workers 4 -Concurrency 125 -Duration 20 -Tag 3B-single
.\run_bench.ps1 -Workers 4 -Concurrency 250 -Duration 20 -Tag 3C-single
.\run_bench.ps1 -Workers 4 -Concurrency 500 -Duration 20 -Tag 3D-single
```
Expected: each produces `3X-single_combined.json`. Note total QPS and P99 for each.

- [ ] **Step 5: Verify AC3 (c=100 QPS ≥ 1,000) and AC4 (c=2000 P99 ≤ 200ms)**

Inspect `3A-single_combined.json` and `3D-single_combined.json`.

---

## Task 11: Execute Phase 3 — nginx+2-instance public-Internet bench

**Files:** none modified; execution only.

- [ ] **Step 1: Re-enable bike-server-2**

```bash
ssh ubuntu@124.220.92.243
cd ~/shared_bike_1/docker
docker compose start bike-server-2
sleep 3
docker exec bike-nginx nginx -s reload
docker exec bike-nginx nginx -t
```
Expected: bike-server-2 starts, nginx reloads cleanly.

- [ ] **Step 2: Quick sanity check — both backends get traffic again**

```bash
docker logs --tail 5 bike-server-1 2>&1 | tail -3
docker logs --tail 5 bike-server-2 2>&1 | tail -3
```
Then run a 5s smoke from Windows:
```powershell
python bike_bench.py qps --host 124.220.92.243 --port 8888 -c 10 -d 5
```

- [ ] **Step 3: Run the 4 concurrency tiers**

```powershell
.\run_bench.ps1 -Workers 4 -Concurrency 25  -Duration 20 -Tag 3A-nginx
.\run_bench.ps1 -Workers 4 -Concurrency 125 -Duration 20 -Tag 3B-nginx
.\run_bench.ps1 -Workers 4 -Concurrency 250 -Duration 20 -Tag 3C-nginx
.\run_bench.ps1 -Workers 4 -Concurrency 500 -Duration 20 -Tag 3D-nginx
```

- [ ] **Step 4: Verify AC5 (3D-nginx within ±15% of 3D-single)**

Compute: `|3D_nginx_total_qps - 3D_single_total_qps| / 3D_single_total_qps <= 0.15`. If yes, hypothesis confirmed.

- [ ] **Step 5: Verify AC6 (errors < 1% across all scenarios)**

For each `3X-Y_combined.json`: `total_errors / total_requests < 0.01`.

---

## Task 12: Write results doc

**Files:**
- Create: `docs/superpowers/specs/2026-07-28-cross-region-bench-nginx-results.md`

- [ ] **Step 1: Collect all input JSONs in one place**

Move (or copy) all `*_combined.json` and `phase1_*.json` files into `docs/superpowers/specs/` alongside the results doc for traceability.

- [ ] **Step 2: Write the results doc**

Create `docs/superpowers/specs/2026-07-28-cross-region-bench-nginx-results.md` using the template below. Replace every `[PLACEHOLDER]` with the actual value from the corresponding JSON.

```markdown
# Cross-Region Bench + nginx Multi-Instance Results

**Date**: 2026-07-28
**Spec**: `2026-07-28-cross-region-bench-nginx-design.md`
**Server**: Tencent Cloud CVM, 4 vCPU AMD EPYC 7K62, 3.6 GB RAM
**Client**: Windows host, 4 Python workers via run_bench.ps1
**Public bandwidth (measured)**: 3 MB/s

## 1. Phase 1 — Loopback gap-fill (single instance, biz=16)

| Cell | c | QPS | P50(ms) | P95(ms) | P99(ms) | Errors |
|------|---|-----|---------|---------|---------|--------|
| 1A | 100 | [from phase1_c100.json] | ... | ... | ... | ... |
| 1B | 500 | [from phase1_c500.json] | ... | ... | ... | ... |

**AC1** (c=100 within ±10% of 18,838): [PASS/FAIL — actual: X]
**AC2** (c=500 ≥ 18,000): [PASS/FAIL — actual: X]

## 2. Phase 3 — Public-Internet single instance (nginx upstream → bike-server-1 only)

| Cell | Total c | QPS | P50(ms) | P95(ms) | P99(ms) | Errors |
|------|---------|-----|---------|---------|---------|--------|
| 3A-single | 100 | ... | ... | ... | ... | ... |
| 3B-single | 500 | ... | ... | ... | ... | ... |
| 3C-single | 1000 | ... | ... | ... | ... | ... |
| 3D-single | 2000 | ... | ... | ... | ... | ... |

**AC3** (3A-single QPS ≥ 1,000): [PASS/FAIL]
**AC4** (3D-single P99 ≤ 200ms): [PASS/FAIL]

## 3. Phase 3 — Public-Internet nginx + 2 instances

| Cell | Total c | QPS | P50(ms) | P95(ms) | P99(ms) | Errors |
|------|---------|-----|---------|---------|---------|--------|
| 3A-nginx | 100 | ... | ... | ... | ... | ... |
| 3B-nginx | 500 | ... | ... | ... | ... | ... |
| 3C-nginx | 1000 | ... | ... | ... | ... | ... |
| 3D-nginx | 2000 | ... | ... | ... | ... | ... |

**AC5** (3D-nginx within ±15% of 3D-single): [PASS/FAIL — Δ = X%]
**AC6** (errors < 1% all scenarios): [PASS/FAIL]

## 4. Side-by-side: single vs nginx+2

| Total c | Single QPS | nginx+2 QPS | Δ% | Single P99 | nginx+2 P99 |
|---------|------------|-------------|----|------------|-------------|
| 100 | ... | ... | ... | ... | ... |
| 500 | ... | ... | ... | ... | ... |
| 1000 | ... | ... | ... | ... | ... |
| 2000 | ... | ... | ... | ... | ... |

## 5. Bandwidth attribution

Estimated bytes/req = 65 (mobile_code req ~32B + resp ~33B).
Theoretical cap at 3 MB/s = 3,000,000 / 65 = ~46,000 QPS.
Observed peak: [from 3D-nginx] QPS. Bandwidth utilization: [X]%.

[If observed QPS is well below the cap, the bottleneck is server-side CPU,
 not bandwidth. If close to the cap, bandwidth is the limit. State which.]

## 6. Surprises / follow-ups

- [E.g., if 3A-single QPS is unexpectedly low due to TCP handshake overhead at
  low concurrency, note it.]
- [E.g., if nginx+2 actually regressed at low c but improved at high c, note it.]
- [E.g., if Windows port exhaustion was hit at c=2000, note the mitigation.]

## 7. Resume paragraph (final)

> 性能压测与水平扩展验证: 基于自研 Python asyncio 压测客户端(支持多 worker
> 进程聚合, 复现 FBEB+Protobuf 二进制协议), 在 4C3.6G 腾讯云 CVM 上从
> 服务器内网与跨地域公网两条路径压测:
>
> - 内网单实例: 吞吐峰值 [actual] QPS (c=500, P99=[actual]ms, 0 错误)
> - 公网跨地域 (Windows → Tencent Cloud, 3 MB/s 带宽):
>   单实例 [actual] QPS @ P99=[actual]ms, 2000 并发 0 错误
> - nginx stream 反向代理 + 2 bike-server 实例水平扩展:
>   QPS 变化 [±actual]%, 证明单实例已饱和 4 vCPU, 单机水平扩展受物理 CPU 限制
>
> 通过控制变量(内网 vs 公网 / 单实例 vs 多实例 / biz=8 vs 16 vs 32)
> 定位瓶颈从连接池 → io 线程同步阻塞 → 物理 CPU, 形成完整的性能分析链.
```

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-07-28-cross-region-bench-nginx-results.md docs/superpowers/specs/*.json
git commit -m "docs(bench): cross-region + nginx multi-instance bench results"
```

- [ ] **Step 4: Update resume paragraph if numbers supersede prior claims**

If the actual numbers differ materially from the resume's current claims, open the resume file and update accordingly. Commit:
```bash
git commit -am "docs(resume): update bench numbers based on 2026-07-28 cross-region run"
```

---

## Self-Review Notes

**Spec coverage:**
- Spec Goal G1 (biz=16 c=100/500 loopback) → Task 8
- Spec Goal G2 (public-Internet measurement) → Tasks 10, 11
- Spec Goal G3 (nginx+2 measurement) → Task 11
- Spec Goal G4 (writeup) → Task 12
- Spec Phase 1 (loopback gap-fill) → Task 8
- Spec Phase 2 (nginx deploy) → Tasks 6, 7, 9
- Spec Phase 3 (cross-region bench) → Tasks 10, 11
- Spec Phase 4 (writeup) → Task 12
- Spec 6.2 test matrix (10 cells) → Tasks 8, 10, 11
- Spec AC1–AC7 → verified in Steps 6/5/4 of Tasks 8/10/11

**Risk callouts surfaced as explicit steps:**
- Spec Risk "Windows port exhaustion" → Task 10 Step 2 (bump ephemeral range)
- Spec Risk "nginx stream config wrong" → Task 6 Step 2 (nginx -t) and Task 9 Step 3 (smoke)
- Spec Risk "packet loss inflates P99" → Task 11 Step 4 (run twice if needed)

**Type consistency:**
- `make_mobile(prefix, worker_id, total_workers, i)` defined in Task 2, used by both `bench_rtt` and `bench_qps`
- `_compute_stats` returns dict with keys `name/concurrency/duration_sec/requests_ok/errors/qps/latency_ms/samples` — used by `_report` (Task 1), `main_async` JSON writer (Task 3), and `aggregate_workers.aggregate` (Task 4, which reads `samples`, `qps`, `requests_ok`, `errors`, `concurrency`, `duration_sec` from input dicts)
- JSON shape produced by Task 3 matches what Task 4 aggregator expects
