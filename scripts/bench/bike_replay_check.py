#!/usr/bin/env python3
"""骑行轨迹回放回归: 验证 0x15 中间点落库与沿轨迹里程 (纯标准库, 零依赖)。

链路: mobile_code -> login -> (recharge) -> nearby -> scan_unlock
      -> 5x position_report(0x15, 单向) -> end_ride -> get_ride_detail
断言:
  1. ride_detail points_count == 中间点数 + 2 (起点 + 终点)
  2. 各点坐标/elapsed_sec 与上报一致, 按时间轴有序
  3. distance_m 明显大于起终点直线距离 (绕行轨迹)
用法: python3 bike_replay_check.py <host> <port>
"""
import math
import socket
import struct
import sys
import time

MAGIC = b'FBEB'
HEADER_LEN = 14

# ---------- minimal protobuf ----------
def enc_varint(n):
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

def enc_str(f, s):
    b = s.encode('utf-8')
    return bytes([(f << 3) | 2]) + enc_varint(len(b)) + b

def enc_int(f, v):
    return bytes([(f << 3) | 0]) + enc_varint(v & 0xFFFFFFFFFFFFFFFF)

def enc_double(f, v):
    return bytes([(f << 3) | 1]) + struct.pack('<d', v)

def read_varint(buf, i):
    v = 0
    shift = 0
    while True:
        b = buf[i]
        i += 1
        v |= (b & 0x7F) << shift
        if not b & 0x80:
            return v, i
        shift += 7

def dec_repeated(body):
    """解码为 {field: [values...]}: varint->int, len-delimited->bytes, wt1->8字节。"""
    fields = {}
    i = 0
    while i < len(body):
        key, i = read_varint(body, i)
        f, wt = key >> 3, key & 7
        if wt == 0:
            v, i = read_varint(body, i)
            fields.setdefault(f, []).append(v)
        elif wt == 2:
            ln, i = read_varint(body, i)
            fields.setdefault(f, []).append(body[i:i + ln])
            i += ln
        elif wt == 1:
            fields.setdefault(f, []).append(body[i:i + 8])
            i += 8
        else:
            raise ValueError(f'unsupported wire type {wt}')
    return fields

def dec_first(body):
    return {f: v[-1] for f, v in dec_repeated(body).items()}

def dec_point(sub):
    """ride_point: lat=1(double) lng=2(double) elapsed_sec=3(varint)"""
    fs = dec_repeated(sub)
    lat = struct.unpack('<d', fs[1][-1])[0] if 1 in fs else 0.0
    lng = struct.unpack('<d', fs[2][-1])[0] if 2 in fs else 0.0
    ela = fs[3][-1] if 3 in fs else 0
    return lat, lng, ela

# ---------- frame ----------
def frame(eid, payload, seq):
    return MAGIC + struct.pack('<HIi', eid, seq, len(payload)) + payload

def recv_exact(sock, n):
    buf = b''
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError('connection closed by peer')
        buf += chunk
    return buf

def recv_frame(sock):
    hdr = recv_exact(sock, HEADER_LEN)
    if hdr[:4] != MAGIC:
        raise ValueError(f'bad magic: {hdr[:4]!r}')
    eid, seq = struct.unpack('<HI', hdr[4:10])
    (length,) = struct.unpack('<i', hdr[10:14])
    body = recv_exact(sock, length) if length > 0 else b''
    return eid, seq, body

def rpc(sock, eid, rsp_eid, payload, seq):
    sock.sendall(frame(eid, payload, seq))
    got_eid, got_seq, body = recv_frame(sock)
    assert got_eid == rsp_eid, f'expect rsp eid 0x{rsp_eid:02x}, got 0x{got_eid:02x}'
    assert got_seq == seq, f'seq mismatch: sent {seq}, got {got_seq}'
    return dec_first(body)

def haversine_m(lat1, lng1, lat2, lng2):
    r = 6371000.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lng2 - lng1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))

def main():
    host = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8888
    mobile = f'137{int(time.time()) % 10**8:08d}'

    sock = socket.create_connection((host, port), timeout=10)
    sock.settimeout(10)
    print(f'[replay] connected {host}:{port}')

    # 1. mobile_code + login
    rsp = rpc(sock, 0x01, 0x02, enc_str(1, mobile), seq=201)
    assert rsp.get(1) == 200, f'mobile_code failed code={rsp.get(1)}'
    icode = rsp.get(2)
    rsp = rpc(sock, 0x03, 0x04, enc_str(1, mobile) + enc_int(2, icode), seq=202)
    assert rsp.get(1) == 200, f'login failed code={rsp.get(1)}'
    token = rsp.get(3, b'')
    token = token.decode() if isinstance(token, bytes) else str(token)
    print(f'[replay] login ok token={token[:12]}...')

    # 2. balance, 不足则充值(只增不删, 安全)
    rsp = rpc(sock, 0x07, 0x08, enc_str(1, token), seq=203)
    bal = rsp.get(2, 0)
    if bal < 200:
        rsp = rpc(sock, 0x05, 0x06, enc_str(1, token) + enc_int(2, 500), seq=204)
        assert rsp.get(1) == 200, f'recharge failed code={rsp.get(1)}'
        bal = rsp.get(2)
        print(f'[replay] recharged, balance={bal}')
    else:
        print(f'[replay] balance={bal} sufficient, skip recharge')

    # 3. nearby 找车 (起点: 种子车集群区 西三环)
    start_lat, start_lng = 39.9820, 116.3180
    payload = enc_str(1, token)
    for f, v in ((2, start_lat), (3, start_lng), (4, 5000.0)):
        payload += enc_double(f, v)
    rsp_all = dec_repeated(rpc_raw_body(sock, 0x11, 0x12, payload, seq=205))
    bikes = rsp_all.get(2, [])
    assert bikes, 'no bikes nearby'
    print(f'[replay] nearby bikes: {len(bikes)}')

    # 4. scan_unlock (逐辆尝试直到成功)
    ride_no = None
    for b in bikes:
        bfs = dec_repeated(b)
        no = bfs[1][-1].decode() if 1 in bfs else ''
        pay = enc_str(1, token) + enc_str(2, no) + enc_double(3, start_lat) + enc_double(4, start_lng)
        rsp = rpc(sock, 0x13, 0x14, pay, seq=206)
        if rsp.get(1) == 200 and rsp.get(3):
            ride_no = rsp.get(3, b'')
            ride_no = ride_no.decode() if isinstance(ride_no, bytes) else str(ride_no)
            print(f'[replay] unlocked bike={no} ride_no={ride_no}')
            break
        print(f'[replay] unlock {no} failed code={rsp.get(1)}, try next')
    assert ride_no, 'all unlock attempts failed'

    # 5. 位置上报 0x15 (单向无响应): 5 个绕行点, seq/elapsed 递增
    #    绕行路径: 向东 -> 向南 -> 向西 -> 向北 -> 东北, 终点偏东南
    reports = [
        (1, 39.9835, 116.3210, 1),
        (2, 39.9815, 116.3235, 2),
        (3, 39.9795, 116.3200, 3),
        (4, 39.9810, 116.3170, 4),
        (5, 39.9828, 116.3195, 5),
    ]
    for seq_no, lat, lng, ela in reports:
        pay = enc_str(1, ride_no) + enc_int(2, seq_no) + enc_double(3, lat) + enc_double(4, lng) + enc_int(5, ela)
        sock.sendall(frame(0x15, pay, 300 + seq_no))
        time.sleep(1.3)   # 真实间隔, 保证 end_ride 的 wall-clock duration > 末点 elapsed
        print(f'[replay] reported seq={seq_no} lat={lat} lng={lng} elapsed={ela}s')
    time.sleep(1.0)

    # 6. end_ride
    end_lat, end_lng = 39.9800, 116.3230
    pay = enc_str(1, token) + enc_str(2, ride_no) + enc_double(3, end_lat) + enc_double(4, end_lng)
    rsp = rpc(sock, 0x17, 0x18, pay, seq=210)
    assert rsp.get(1) == 200, f'end_ride failed code={rsp.get(1)} desc={rsp.get(2)}'
    end_dist, duration = rsp.get(4, 0), rsp.get(3, 0)
    print(f'[replay] end_ride ok duration={duration}s distance_m={end_dist} amount={rsp.get(5)}')

    # 7. ride_detail 断言
    time.sleep(0.5)
    pay = enc_str(1, token) + enc_str(2, ride_no)
    rsp_all = dec_repeated(rpc_raw_body(sock, 0x1B, 0x1C, pay, seq=211))
    assert rsp_all.get(1, [-1])[-1] == 200, f'ride_detail failed code={rsp_all.get(1)}'
    detail_dist = rsp_all.get(4, [0])[-1]
    pts = [dec_point(p) for p in rsp_all.get(8, [])]
    print(f'[replay] ride_detail points_count={len(pts)} distance_m={detail_dist}')
    for i, (la, ln, el) in enumerate(pts):
        print(f'[replay]   pt[{i}] lat={la:.6f} lng={ln:.6f} elapsed={el}s')

    expect_n = len(reports) + 2
    assert len(pts) == expect_n, f'points_count={len(pts)}, expect {expect_n} (5 mid + start + end)'
    # 起点/终点
    assert abs(pts[0][0] - start_lat) < 1e-6 and abs(pts[0][1] - start_lng) < 1e-6, 'start point mismatch'
    assert pts[0][2] == 0, 'start elapsed != 0'
    assert abs(pts[-1][0] - end_lat) < 1e-6 and abs(pts[-1][1] - end_lng) < 1e-6, 'end point mismatch'
    # 中间点逐一比对
    for i, (seq_no, lat, lng, ela) in enumerate(reports):
        la, ln, el = pts[1 + i]
        assert abs(la - lat) < 1e-6 and abs(ln - lng) < 1e-6, f'mid point {i} coord mismatch'
        assert el == ela, f'mid point {i} elapsed mismatch: {el} != {ela}'
    # 时间轴有序
    elas = [p[2] for p in pts]
    assert elas == sorted(elas), f'points not time-ordered: {elas}'
    # 绕行里程 > 起终点直线距离
    straight = haversine_m(start_lat, start_lng, end_lat, end_lng)
    print(f'[replay] straight={straight:.0f}m vs track={detail_dist}m')
    assert detail_dist > straight * 1.2, f'distance {detail_dist} not clearly > straight {straight:.0f}'

    sock.close()
    print('[replay] PASS — 中间点落库/坐标时间轴/绕行里程 全部断言通过')

def rpc_raw_body(sock, eid, rsp_eid, payload, seq):
    got_eid, got_seq, body = (sock.sendall(frame(eid, payload, seq)), recv_frame(sock))[1]
    assert got_eid == rsp_eid, f'expect rsp eid 0x{rsp_eid:02x}, got 0x{got_eid:02x}'
    assert got_seq == seq, f'seq mismatch'
    return body

if __name__ == '__main__':
    main()
