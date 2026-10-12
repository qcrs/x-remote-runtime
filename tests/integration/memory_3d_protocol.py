#!/usr/bin/env python3
"""Check opcode 56 legacy response compatibility and new submit retirement."""
import os
import socket
import struct

MAGIC, VERSION = 0x43525839, 3
request_id = 0


def recv_exact(sock, count):
    data = bytearray()
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise RuntimeError("short response")
        data.extend(chunk)
    return bytes(data)


def rpc(sock, opcode, payload=b"", status=0):
    global request_id
    request_id += 1
    sock.sendall(struct.pack("!5I", MAGIC, VERSION, opcode, request_id, len(payload)) + payload)
    header = struct.unpack("!6I", recv_exact(sock, 24))
    assert header[:5] == (MAGIC, VERSION, opcode, request_id, status), header
    return recv_exact(sock, header[5])


def dto(mode, allocation, stream, data=b""):
    # Host staging is compact; device region is 17 bytes x 5 rows x 3 slices.
    did, sid = (allocation, 0) if mode in (1, 4) else (0, allocation)
    return struct.pack("!I12Q", mode, did, 0, 17, 85, sid, 0, 17, 85, 17, 5, 3, stream) + data


with socket.create_connection((os.getenv("COREX_REMOTE_HOST", "127.0.0.1"),
                               int(os.getenv("COREX_REMOTE_PORT", "50082"))), timeout=10) as sock:
    allocation, = struct.unpack("!Q", rpc(sock, 1, struct.pack("!Q", 255)))
    stream, = struct.unpack("!Q", rpc(sock, 12, struct.pack("!I", 0)))
    data = bytes((i + 1) % 256 for i in range(255))
    assert rpc(sock, 56, dto(1, allocation, stream, data)) == b""
    assert rpc(sock, 56, dto(2, allocation, stream)) == data
    print("OPCODE_56_LEGACY_MODES_1_2_RESPONSE_COMPATIBILITY=PASS")
    transfer, = struct.unpack("!Q", rpc(sock, 56, dto(4, allocation, stream, data)))
    assert rpc(sock, 25, struct.pack("!Q", transfer)) == b""
    transfer, = struct.unpack("!Q", rpc(sock, 56, dto(5, allocation, 0)))
    state, = struct.unpack("!I", rpc(sock, 24, struct.pack("!Q", transfer)))
    assert state in (0, 1)
    assert rpc(sock, 25, struct.pack("!Q", transfer)) == data
    assert rpc(sock, 24, struct.pack("!Q", transfer), status=2) == b""
    print("OPCODE_56_SUBMIT_MODES_4_5_QUERY_WAIT_RETIREMENT=PASS")
    malformed = struct.pack("!I12Q", 3, allocation, 0, 0, 85,
                            allocation, 0, 17, 85, 17, 5, 3, stream)
    assert rpc(sock, 56, malformed, status=1) == b""
    print("OPCODE_56_INVALID_DEVICE_PITCH_REJECTED=PASS")
    rpc(sock, 13, struct.pack("!Q", stream))
    rpc(sock, 6, struct.pack("!Q", allocation))
    rpc(sock, 7)
print("MEMORY_3D_PROTOCOL=PASS")
