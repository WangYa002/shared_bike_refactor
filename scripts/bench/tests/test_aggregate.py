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
