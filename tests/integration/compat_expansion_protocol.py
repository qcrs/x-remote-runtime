#!/usr/bin/env python3
"""Independent CRX9 fixtures for the new bounded control/object RPCs."""
import os
import socket
import struct


def exact(sock, length):
    result = b""
    while len(result) < length:
        part = sock.recv(length - len(result))
        if not part:
            raise RuntimeError("unexpected EOF")
        result += part
    return result


with socket.create_connection(("127.0.0.1", int(os.environ["COREX_REMOTE_PORT"]))) as sock:
    sock.settimeout(10)
    request_id = 0

    def call(opcode, payload=b"", expected=0):
        global request_id
        request_id += 1
        sock.sendall(struct.pack("!IIIII", 0x43525839, 3, opcode, request_id, len(payload)) + payload)
        header = struct.unpack("!IIIIII", exact(sock, 24))
        assert header[:4] == (0x43525839, 3, opcode, request_id), header
        body = exact(sock, header[5])
        assert header[4] == expected, (opcode, header)
        return body

    for opcode, size in ((59, 12), (60, 4), (61, 4), (62, 16), (63, 12), (64, 52)):
        call(opcode, b"\0" * (size - 1), expected=1)
        call(opcode, b"\0" * (size + 1), expected=1)
    call(59, struct.pack("!iQ", 99, 1), expected=1)
    call(60, struct.pack("!i", 99), expected=1)
    call(61, struct.pack("!i", 99), expected=1)
    call(62, struct.pack("!QQ", 999, 0), expected=2)
    old_cache = call(43)
    assert len(old_cache) == 4
    assert call(60, bytes.fromhex("00000001")) == b""
    assert call(43) == bytes.fromhex("00000001")
    call(60, old_cache)
    limit = call(42, bytes.fromhex("00000001"))
    assert len(limit) == 8
    assert call(59, bytes.fromhex("00000001") + limit) == b""
    assert call(42, bytes.fromhex("00000001")) == limit
    call(59, struct.pack("!iQ", 0, 1024), expected=3)
    call(61, bytes.fromhex("00000001"))
    call(61, bytes.fromhex("00000000"))
    get_window = struct.pack("!Qi", 0, 1)
    set_window = get_window + bytes(40)
    assert call(64, set_window) == b""
    assert call(63, get_window) == bytes(40)
    call(63, struct.pack("!Qi", 999, 1), expected=2)
    call(64, struct.pack("!Qi", 0, 99) + bytes(40), expected=1)
    # Missing allocation and NaN float are rejected at the trust boundary.
    call(64, get_window + struct.pack("!QQQIiii", 999, 0, 32, 0x3f000000, 0, 0, 0), expected=1)
    call(64, get_window + struct.pack("!QQQIiii", 0, 0, 0, 0x7fc00000, 0, 0, 0), expected=1)
    call(63, struct.pack("!Qi", 0, 3), expected=3)
    call(64, struct.pack("!Qi", 0, 3) + bytes(40), expected=3)
    call(7)
print("COMPAT_CONTROL_GOLDEN_WIRE_AND_NEGATIVES=PASS")
