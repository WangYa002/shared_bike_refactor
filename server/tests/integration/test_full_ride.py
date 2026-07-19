"""End-to-end: login -> recharge -> scan -> wait -> end -> verify deduction.

Requires a running bike-server reachable at $BIKE_TEST_SERVER:$BIKE_TEST_PORT.
Requires bike_pb2.py generated under server/tests/integration/_proto/.
"""
import os
import sys
import time
import pathlib

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import fbeb_client
from fbeb_client import FBEBClient
fbeb_client._load_messages()  # ensures _proto/ is on sys.path before we import bike_pb2
import bike_pb2

SERVER = os.environ.get("BIKE_TEST_SERVER", "127.0.0.1")
PORT   = int(os.environ.get("BIKE_TEST_PORT", "18888"))
MOBILE = "15600000010"


@pytest.fixture(scope="module")
def client():
    c = FBEBClient(SERVER, PORT)
    yield c
    c.close()


def login(client):
    req = bike_pb2.mobile_request(mobile=MOBILE)
    rsp = bike_pb2.mobile_response()
    rsp.ParseFromString(client.call(0x01, req))
    assert rsp.code() == 200
    icode = rsp.icode()

    lr = bike_pb2.login_request(mobile=MOBILE, icode=icode)
    lrsp = bike_pb2.login_response()
    lrsp.ParseFromString(client.call(0x03, lr))
    assert lrsp.code() == 200
    return lrsp.session_token()


def test_complete_ride_deducts_balance(client):
    token = login(client)

    # Recharge 10 yuan (1000 fen)
    rc = bike_pb2.recharge_request(session_token=token, amount=1000)
    rcr = bike_pb2.recharge_response()
    rcr.ParseFromString(client.call(0x05, rc))
    assert rcr.code() == 200

    # Pick a known idle bike from 03_seed_bikes.sql (BJ-000001..BJ-000057 are idle)
    su = bike_pb2.scan_unlock_request(session_token=token, bike_no="BJ-000001",
                                       lat=39.982, lng=116.314)
    sur = bike_pb2.scan_unlock_response()
    sur.ParseFromString(client.call(0x13, su))
    assert sur.code() == 200, f"scan_unlock failed: {sur.desc()}"
    ride_no = sur.ride_no()

    # Ride for 20 seconds (above the 15-min threshold would cost more; under 15 min costs the base 100 fen)
    time.sleep(20)

    er = bike_pb2.end_ride_request(session_token=token, ride_no=ride_no,
                                    end_lat=39.985, end_lng=116.318)
    err = bike_pb2.end_ride_response()
    err.ParseFromString(client.call(0x17, er))
    assert err.code() == 200, f"end_ride failed: {err.desc()}"
    # Base fee: 1.00 yuan = 100 fen for <=15 min ride
    assert err.amount_cent() == 100

    # Check balance
    bal = bike_pb2.account_balance_request(session_token=token)
    balr = bike_pb2.account_balance_response()
    balr.ParseFromString(client.call(0x07, bal))
    # 1000 - 100 = 900 (modulo prior runs leaving state behind -- re-running this test
    # against the same user will accumulate balance. If that becomes flaky, switch to
    # a fresh mobile per run.)
    assert balr.balance() == 900

    # Check history
    lr = bike_pb2.list_rides_request(session_token=token, limit=20)
    lrr = bike_pb2.list_rides_response()
    lrr.ParseFromString(client.call(0x1D, lr))
    assert lrr.code() == 200
    assert any(r.ride_no() == ride_no for r in lrr.rides())

    # Check detail
    dr = bike_pb2.get_ride_detail_request(session_token=token, ride_no=ride_no)
    drr = bike_pb2.get_ride_detail_response()
    drr.ParseFromString(client.call(0x1B, dr))
    assert drr.code() == 200
    assert drr.points_size() >= 2
