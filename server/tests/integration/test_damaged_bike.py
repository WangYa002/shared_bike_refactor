"""Scan a damaged bike returns 409."""
import os
import sys
import pathlib

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import fbeb_client
fbeb_client._load_messages()
import bike_pb2
from fbeb_client import FBEBClient
from test_full_ride import login

SERVER = os.environ.get("BIKE_TEST_SERVER", "127.0.0.1")
PORT   = int(os.environ.get("BIKE_TEST_PORT", "18888"))


def test_scan_damaged_bike_returns_409():
    c = FBEBClient(SERVER, PORT)
    try:
        token = login(c)
        # BJ-000058/059/060 are seeded with status=2 (Damaged) in 03_seed_bikes.sql
        su = bike_pb2.scan_unlock_request(session_token=token, bike_no="BJ-000058",
                                           lat=39.971, lng=116.329)
        r = bike_pb2.scan_unlock_response()
        r.ParseFromString(c.call(0x13, su))
        assert r.code() == 409
    finally:
        c.close()
