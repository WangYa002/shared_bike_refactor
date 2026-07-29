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


class TestMakeMobile(unittest.TestCase):
    def test_default_no_partition(self):
        from bike_bench import make_mobile
        # worker_id=0, total_workers=1: behaves like the old code
        self.assertEqual(make_mobile("139", 0, 1, 0), "13900000000")
        self.assertEqual(make_mobile("139", 0, 1, 99), "13900000099")

    def test_workers_get_disjoint_ranges(self):
        from bike_bench import make_mobile
        # Each worker gets a 100K-number block: worker N starts at N*100_000
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


if __name__ == '__main__':
    unittest.main()
