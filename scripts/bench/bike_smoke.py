#!/usr/bin/env python3
"""部署冒烟测试: FBEB v2 帧端到端验证 (纯标准库, 零依赖)。

链路: mobile_code(0x01) -> login(0x03) -> account_balance(0x07) -> list_nearby_bikes(0x11)
验证点:
  1. 帧头 14 字节 (FBEB + eid u16 LE + seq u32 LE + len i32 LE) 往返
  2. seq 原样回带
  3. 请求经 nginx -> gateway(io_uring) -> shm 环 -> dispatch -> MySQL 全链路
用法: python3 bike_smoke.py <host> <port>
"""
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

def dec_fields(body):
    """解码 protobuf 为 {field: value} (varint->int, len-delimited->bytes)。"""
    fields = {}
    i = 0
    while i < len(body):
        key, i = read_varint(body, i)
        f, wt = key >> 3, key & 7
        if wt == 0:
            v, i = read_varint(body, i)
            fields[f] = v
        elif wt == 2:
            ln, i = read_varint(body, i)
            fields[f] = body[i:i + ln]
            i += ln
        elif wt == 1:
            fields[f] = body[i:i + 8]
            i += 8
        elif wt == 5:
            fields[f] = body[i:i + 4]
            i += 4
        else:
            raise ValueError(f'unsupported wire type {wt}')
    return fields

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
    t0 = time.perf_counter()
    sock.sendall(frame(eid, payload, seq))
    got_eid, got_seq, body = recv_frame(sock)
    dt = (time.perf_counter() - t0) * 1000
    assert got_eid == rsp_eid, f'expect rsp eid 0x{rsp_eid:02x}, got 0x{got_eid:02x}'
    assert got_seq == seq, f'seq mismatch: sent {seq}, got {got_seq}'
    return dec_fields(body), dt

def main():
    host = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8888
    mobile = f'138{int(time.time()) % 10**8:08d}'

    sock = socket.create_connection((host, port), timeout=10)
    sock.settimeout(10)
    print(f'[smoke] connected {host}:{port}')

    # 1. mobile_code
    rsp, dt = rpc(sock, 0x01, 0x02, enc_str(1, mobile), seq=101)
    code, icode = rsp.get(1, -1), rsp.get(2, 0)
    print(f'[smoke] mobile_code mobile={mobile} code={code} icode={icode} ({dt:.1f}ms)')
    assert code == 200, f'mobile_code failed code={code}'   # ErrCode::Ok == 200

    # 2. login
    rsp, dt = rpc(sock, 0x03, 0x04, enc_str(1, mobile) + enc_int(2, icode), seq=102)
    code = rsp.get(1, -1)
    token = rsp.get(3, b'')
    token = token.decode() if isinstance(token, bytes) else str(token)
    print(f'[smoke] login code={code} token={token[:16]}... ({dt:.1f}ms)')
    assert code == 200 and token, f'login failed code={code}'

    # 3. account_balance
    rsp, dt = rpc(sock, 0x07, 0x08, enc_str(1, token), seq=103)
    print(f'[smoke] balance code={rsp.get(1)} balance={rsp.get(2)} ({dt:.1f}ms)')
    assert rsp.get(1) == 200, 'balance failed'

    # 4. list_nearby_bikes (double 字段 field2/3/4, wire type 1)
    payload = enc_str(1, token)
    for f, v in ((2, 39.98), (3, 116.32), (4, 5000.0)):
        payload += bytes([(f << 3) | 1]) + struct.pack('<d', v)
    rsp, dt = rpc(sock, 0x11, 0x12, payload, seq=104)
    bikes = [v for k, v in rsp.items() if k == 2] if False else None
    # repeated bike_info 是 len-delimited, dec_fields 只留最后一个, 单独计数
    body_fields = rsp
    print(f'[smoke] nearby code={body_fields.get(1)} ({dt:.1f}ms)')
    assert body_fields.get(1) == 200, 'nearby failed'

    sock.close()
    print('[smoke] PASS — 端到端链路 mobile_code->login->balance->nearby 全部成功')

if __name__ == '__main__':
    main()
