#!/usr/bin/env python3
import os
from pathlib import Path
import socket
import struct

MAGIC = 0x43525839
VERSION = 3
ST_OK = 0
ST_BAD_REQUEST = 1
ST_NOT_FOUND = 2
OP_SYNC = 4
OP_CLOSE = 7
OP_STREAM_QUERY = 14
OP_EVENT_QUERY = 19


def exact(sock, length):
    data = b""
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise RuntimeError("unexpected EOF")
        data += chunk
    return data


def call(sock, opcode, request_id, payload=b""):
    sock.sendall(struct.pack("!IIIII", MAGIC, VERSION, opcode, request_id, len(payload)) + payload)
    header = struct.unpack("!IIIIII", exact(sock, 24))
    body = exact(sock, header[5])
    if header[2:4] != (opcode, request_id):
        raise RuntimeError("response identity mismatch")
    return header[4], body


def main():
    port = int(os.environ.get("COREX_REMOTE_PORT", "50051"))
    with socket.create_connection((os.environ.get("COREX_REMOTE_HOST", "127.0.0.1"), port)) as sock:
        if call(sock, OP_SYNC, 1)[0] != ST_OK:
            raise RuntimeError("generated sync parity failed")
        if call(sock, OP_STREAM_QUERY, 2, struct.pack("!Q", 999))[0] != ST_NOT_FOUND:
            raise RuntimeError("generated stream query parity failed")
        if call(sock, OP_STREAM_QUERY, 3, b"bad")[0] != ST_BAD_REQUEST:
            raise RuntimeError("generated stream negative failed")
        if call(sock, OP_EVENT_QUERY, 4)[0] != ST_BAD_REQUEST:
            raise RuntimeError("generated event negative failed")
        call(sock, OP_CLOSE, 5)
    print("M2_S5_GENERATED_API_PARITY=PASS")


if __name__ == "__main__":
    main()
