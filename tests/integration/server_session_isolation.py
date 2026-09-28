#!/usr/bin/env python3
import os
from pathlib import Path
import socket
import struct

MAGIC = 0x43525839
VERSION = 3
ST_OK = 0
ST_NOT_FOUND = 2
OP_ALLOC = 1
OP_H2D = 2
OP_CLOSE = 7
OP_CREATE_STREAM = 12
OP_STREAM_QUERY = 14
OP_CREATE_EVENT = 16
OP_EVENT_QUERY = 19
OP_UPLOAD_MODULE = 8
OP_GET_KERNEL = 9
OP_FUNCTION_SET_ATTRIBUTE = 45
OP_FUNCTION_SET_CACHE_CONFIG = 46


def rpc(sock, opcode, request_id, payload=b""):
    sock.sendall(struct.pack("!IIIII", MAGIC, VERSION, opcode, request_id, len(payload)))
    if payload:
        sock.sendall(payload)
    header = recv_exact(sock, 24)
    magic, version, returned_opcode, returned_id, status, length = struct.unpack(
        "!IIIIII", header
    )
    if (magic, version, returned_opcode, returned_id) != (
        MAGIC,
        VERSION,
        opcode,
        request_id,
    ):
        raise RuntimeError("response header mismatch")
    return status, recv_exact(sock, length)


def recv_exact(sock, length):
    chunks = []
    remaining = length
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise RuntimeError("unexpected EOF")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def connect():
    host = os.environ.get("COREX_REMOTE_HOST", "127.0.0.1")
    port = int(os.environ.get("COREX_REMOTE_PORT", "50051"))
    return socket.create_connection((host, port), timeout=10)


def expect(name, actual, expected):
    result = "PASS" if actual == expected else "FAIL"
    print(f"M1_S4_CHECK name={name} actual={actual} expected={expected} result={result}")
    if actual != expected:
        raise RuntimeError(name)


def main():
    first = connect()
    status, payload = rpc(first, OP_ALLOC, 1, struct.pack("!Q", 4))
    expect("session-a-alloc", status, ST_OK)
    allocation_a = struct.unpack("!Q", payload)[0]
    status, payload = rpc(first, OP_CREATE_STREAM, 2, struct.pack("!I", 0))
    expect("session-a-stream", status, ST_OK)
    stream_a = struct.unpack("!Q", payload)[0]
    status, payload = rpc(first, OP_CREATE_EVENT, 3, struct.pack("!I", 0))
    expect("session-a-event", status, ST_OK)
    event_a = struct.unpack("!Q", payload)[0]

    cubin_path = Path(__file__).resolve().parents[2] / "build/session_lifecycle.cubin"
    image = cubin_path.read_bytes()
    module_name = b"session-isolation.cubin"
    upload = (struct.pack("!I", len(module_name)) + module_name +
              struct.pack("!Q", len(image)) + image)
    status, payload = rpc(first, OP_UPLOAD_MODULE, 4, upload)
    expect("session-a-module", status, ST_OK)
    module_a = struct.unpack("!Q", payload)[0]
    kernel_name = b"m1s2_lifecycle_kernel"
    status, payload = rpc(
        first, OP_GET_KERNEL, 5,
        struct.pack("!QI", module_a, len(kernel_name)) + kernel_name)
    expect("session-a-kernel", status, ST_OK)
    kernel_a = struct.unpack("!Q", payload)[0]
    first.close()  # Retire A's live IDs before the serial server accepts B.

    second = connect()
    status, _ = rpc(
        second,
        OP_H2D,
        1,
        struct.pack("!QQQ", allocation_a, 0, 1) + b"x",
    )
    expect("session-a-allocation-hidden-from-b", status, ST_NOT_FOUND)
    status, _ = rpc(second, OP_STREAM_QUERY, 2, struct.pack("!Q", stream_a))
    expect("session-a-stream-hidden-from-b", status, ST_NOT_FOUND)
    status, _ = rpc(second, OP_EVENT_QUERY, 3, struct.pack("!Q", event_a))
    expect("session-a-event-hidden-from-b", status, ST_NOT_FOUND)
    status, _ = rpc(second, OP_FUNCTION_SET_ATTRIBUTE, 4,
                    struct.pack("!Qii", kernel_a, 8, 0))
    expect("session-a-kernel-hidden-from-b", status, ST_NOT_FOUND)
    status, _ = rpc(second, OP_FUNCTION_SET_CACHE_CONFIG, 5,
                    struct.pack("!Qi", kernel_a, 0))
    expect("session-a-kernel-cache-hidden-from-b", status, ST_NOT_FOUND)

    status, payload = rpc(second, OP_ALLOC, 6, struct.pack("!Q", 4))
    expect("session-b-alloc", status, ST_OK)
    allocation_b = struct.unpack("!Q", payload)[0]
    if allocation_b != 1:
        raise RuntimeError("session B allocation counter did not restart")
    status, payload = rpc(second, OP_CREATE_STREAM, 7, struct.pack("!I", 0))
    expect("session-b-stream", status, ST_OK)
    if struct.unpack("!Q", payload)[0] != 1:
        raise RuntimeError("session B stream counter did not restart")
    status, _ = rpc(second, OP_CLOSE, 8)
    expect("session-b-close", status, ST_OK)
    second.close()

    print("M1_S4_SERVER_SESSION_ISOLATION=PASS")


if __name__ == "__main__":
    main()
