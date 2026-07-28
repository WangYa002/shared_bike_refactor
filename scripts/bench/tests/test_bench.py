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
