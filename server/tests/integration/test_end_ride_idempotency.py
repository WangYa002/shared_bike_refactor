"""END_RIDE twice returns history, no double deduction."""
import os
import sys
import time
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


def test_end_ride_idempotent():
    c = FBEBClient(SERVER, PORT)
    try:
        token = login(c)
        # Recharge
        rc = bike_pb2.recharge_request(session_token=token, amount=1000)
        c.call(0x05, rc)
        # Scan a fresh bike (BJ-000002 — different from test_full_ride to avoid 408)
        su = bike_pb2.scan_unlock_request(session_token=token, bike_no="BJ-000002",
                                           lat=39.978, lng=116.319)
        sur = bike_pb2.scan_unlock_response()
        sur.ParseFromString(c.call(0x13, su))
        assert sur.code() == 200
        ride_no = sur.ride_no()

        # End once
        er = bike_pb2.end_ride_request(session_token=token, ride_no=ride_no,
                                        end_lat=39.980, end_lng=116.321)
        err1 = bike_pb2.end_ride_response()
        err1.ParseFromString(c.call(0x17, er))
        assert err1.code() == 200
        amt1 = err1.amount_cent()
        bal1 = err1.balance_after()

        # End again — should return same history, no second deduction
        err2 = bike_pb2.end_ride_response()
        err2.ParseFromString(c.call(0x17, er))
        assert err2.code() == 200
        assert err2.amount_cent() == amt1
        assert err2.balance_after() == bal1
    finally:
        c.close()
