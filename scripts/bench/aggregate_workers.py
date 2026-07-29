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

    out = json.dumps(combined, indent=2)
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
