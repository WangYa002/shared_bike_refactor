"""Minimal FBEB client for integration tests.

Wire format: 'FBEB' + uint16_le event_id + uint32_le length + payload
"""
import socket
import struct
import sys
import pathlib


def _load_messages():
    """Import the protoc-generated bike_pb2 module.

    Expects bike_pb2.py to live in server/tests/integration/_proto/.
    The deployment script (S26) generates it via:
        protoc --python_out=server/tests/integration/_proto \\
               --proto_path=proto proto/bike.proto
    """
    proto_dir = pathlib.Path(__file__).parent / "_proto"
    if str(proto_dir) not in sys.path:
        sys.path.insert(0, str(proto_dir))
    import bike_pb2
    return bike_pb2


class FBEBClient:
    MAGIC = b'FBEB'

    def __init__(self, host: str, port: int):
        self._sock = socket.create_connection((host, port))
        self._pb = _load_messages()

    @property
    def pb(self):
        """Access the loaded bike_pb2 module for message construction."""
        return self._pb

    def _send(self, event_id: int, payload: bytes) -> None:
        header = self.MAGIC + struct.pack('<HI', event_id, len(payload))
        self._sock.sendall(header + payload)

    def _recvn(self, n: int) -> bytes:
        buf = b''
        while len(buf) < n:
            chunk = self._sock.recv(min(n - len(buf), 8192))
            if not chunk:
                raise ConnectionError("socket closed")
            buf += chunk
        return buf

    def _recv(self) -> tuple[int, bytes]:
        header = self._recvn(10)
        if header[:4] != self.MAGIC:
            raise ConnectionError(f"bad magic {header[:4]!r}")
        event_id, length = struct.unpack('<HI', header[4:])
        payload = self._recvn(length) if length else b''
        return event_id, payload

    def call(self, event_id: int, request_msg) -> bytes:
        """Round-trip: send request, return response payload bytes."""
        self._send(event_id, request_msg.SerializeToString())
        _, payload = self._recv()
        return payload

    def send_oneway(self, event_id: int, request_msg) -> None:
        """Fire-and-forget (for events like position_report)."""
        self._send(event_id, request_msg.SerializeToString())

    def close(self):
        self._sock.close()
