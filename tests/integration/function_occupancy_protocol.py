#!/usr/bin/env python3
import os
import socket
import struct

MAGIC = 0x43525839
VERSION = 3
ST_BAD_REQUEST = 1
ST_NOT_FOUND = 2
OP_CLOSE = 7
OP_FUNCTION_ATTRIBUTES = 37
OP_OCCUPANCY = 38


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
    if header[:4] != (MAGIC, VERSION, opcode, request_id):
        raise RuntimeError("response identity mismatch")
    return header[4], body


def expect(sock, opcode, request_id, payload, expected):
    status, body = call(sock, opcode, request_id, payload)
    if status != expected or body:
        raise RuntimeError(
            f"opcode {opcode} status/body mismatch: status={status} bytes={len(body)}"
        )


def main():
    port = int(os.environ.get("COREX_REMOTE_PORT", "50051"))
    host = os.environ.get("COREX_REMOTE_HOST", "127.0.0.1")
    with socket.create_connection((host, port), timeout=10) as sock:
        expect(sock, OP_FUNCTION_ATTRIBUTES, 1, b"bad", ST_BAD_REQUEST)
        expect(sock, OP_FUNCTION_ATTRIBUTES, 2, struct.pack("!Q", 999), ST_NOT_FOUND)
        expect(sock, OP_OCCUPANCY, 3, b"bad", ST_BAD_REQUEST)
        expect(sock, OP_OCCUPANCY, 4, struct.pack("!QIQ", 999, 256, 0), ST_BAD_REQUEST)
        call(sock, OP_CLOSE, 5)
    print("M3_S5_FUNCTION_OCCUPANCY_PROTOCOL=PASS")


if __name__ == "__main__":
    main()
