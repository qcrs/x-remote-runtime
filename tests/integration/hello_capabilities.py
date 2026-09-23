#!/usr/bin/env python3
"""M2-S2 wire and client negotiation integration checks."""

import ctypes
import os
from pathlib import Path
import socket
import struct
import threading

from session_failure_reconnect import start_server, stop_server

MAGIC = 0x43525839
VERSION = 3
HELLO = 27
SYNC = 4
CLOSE = 7
ST_OK = 0
ST_BAD_REQUEST = 1


def recv_exact(connection, length):
    data = bytearray()
    while len(data) < length:
        chunk = connection.recv(length - len(data))
        if not chunk:
            raise RuntimeError("unexpected EOF")
        data.extend(chunk)
    return bytes(data)


def request(connection, opcode, req_id, payload=b""):
    connection.sendall(struct.pack("!IIIII", MAGIC, VERSION, opcode, req_id, len(payload)) + payload)
    header = struct.unpack("!IIIIII", recv_exact(connection, 24))
    body = recv_exact(connection, header[5])
    if header[:2] != (MAGIC, VERSION) or header[2:4] != (opcode, req_id):
        raise RuntimeError(f"wrong response header: {header}")
    return header[4], body


def fake_peer(port, malformed):
    ready = threading.Event()
    failures = []

    def serve():
        try:
            with socket.socket() as listener:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                listener.bind(("127.0.0.1", port))
                listener.listen(1)
                ready.set()
                connection, _ = listener.accept()
                with connection:
                    while True:
                        header = struct.unpack("!IIIII", recv_exact(connection, 20))
                        magic, version, opcode, req_id, length = header
                        recv_exact(connection, length)
                        if (magic, version) != (MAGIC, VERSION):
                            raise RuntimeError("invalid request protocol")
                        if opcode == HELLO:
                            if malformed:
                                # Fixed fields claim two capabilities, but only one is present.
                                fields = (MAGIC, VERSION, 1, 1, 1, 0, 1, 4400, 1, 1, 2, 1)
                                body = struct.pack("!" + "I" * len(fields), *fields)
                                status = ST_OK
                            else:
                                body = b""
                                status = ST_BAD_REQUEST
                        elif opcode in (SYNC, CLOSE) and not malformed:
                            body = b""
                            status = ST_OK
                        else:
                            raise RuntimeError(f"unexpected opcode {opcode}")
                        connection.sendall(struct.pack("!IIIIII", MAGIC, VERSION, opcode, req_id, status, len(body)) + body)
                        if malformed or opcode in (SYNC, CLOSE):
                            return
        except Exception as exc:
            failures.append(exc)
            ready.set()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    if not ready.wait(5):
        raise RuntimeError("fake peer did not become ready")
    if failures:
        raise failures[0]
    return thread, failures


def check_peer(thread, failures):
    thread.join(5)
    if thread.is_alive():
        raise RuntimeError("fake peer did not complete")
    if failures:
        raise failures[0]


def main():
    root = Path(__file__).resolve().parents[2]
    output = Path(os.environ.get("OUT", root / "evidence/m2/s2/hello"))
    output.mkdir(parents=True, exist_ok=True)
    port = int(os.environ.get("M2_S2_TEST_PORT", "50072"))
    os.environ["COREX_REMOTE_HOST"] = "127.0.0.1"
    os.environ["COREX_REMOTE_PORT"] = str(port)
    runtime = ctypes.CDLL(str(root / "dist/lib/libcorex_remote_cudart.so.1.1.0"))
    runtime.cudaDeviceSynchronize.restype = ctypes.c_int
    runtime.cudaGetDeviceCount.argtypes = [ctypes.POINTER(ctypes.c_int)]
    runtime.cudaGetDeviceCount.restype = ctypes.c_int

    server, log = start_server(str(root / "dist/bin/runtime_server"), port, output / "server.log")
    try:
        with socket.create_connection(("127.0.0.1", port)) as connection:
            status, body = request(connection, HELLO, 1)
            if status != ST_OK or len(body) < 44 or len(body) % 4:
                raise RuntimeError("HELLO response size/status invalid")
            values = struct.unpack("!" + "I" * (len(body) // 4), body)
            if values[:3] != (MAGIC, VERSION, 1) or values[6] != 1 or values[8:10] != (1, 1):
                raise RuntimeError(f"HELLO identity invalid: {values}")
            if values[10] != len(values) - 11 or values[11:] != (1, 2, 3, 4, 5, 6):
                raise RuntimeError(f"HELLO capabilities invalid: {values}")
            status, body = request(connection, HELLO, 2, b"bad")
            if status != ST_BAD_REQUEST or body:
                raise RuntimeError("nonempty HELLO request was accepted")
            status, body = request(connection, 999, 3)
            if status != ST_BAD_REQUEST or body:
                raise RuntimeError("unknown opcode did not return ST_BAD_REQUEST")
            status, body = request(connection, HELLO, 4)
            if status != ST_OK or len(body) < 44:
                raise RuntimeError("unknown opcode corrupted the connection")
            request(connection, CLOSE, 5)
            print("M2_S3_UNKNOWN_OPCODE=PASS")
        if runtime.cudaDeviceSynchronize() != 0:
            raise RuntimeError("new client/new server sync failed")
        print("M2_S2_NEW_PEER=PASS")
    finally:
        stop_server(server, log)

    if runtime.cudaDeviceSynchronize() == 0:
        raise RuntimeError("stopped server was not detected")

    legacy, failures = fake_peer(port, malformed=False)
    if runtime.cudaDeviceSynchronize() != 0:
        raise RuntimeError("legacy V3 fallback did not preserve sync")
    check_peer(legacy, failures)
    print("M2_S2_LEGACY_V3=PASS")

    if runtime.cudaDeviceSynchronize() == 0:
        raise RuntimeError("closed legacy peer was not detected")

    malformed, failures = fake_peer(port, malformed=True)
    count = ctypes.c_int()
    if runtime.cudaGetDeviceCount(ctypes.byref(count)) != 3:
        raise RuntimeError("malformed HELLO did not fail initialization")
    check_peer(malformed, failures)
    print("M2_S2_MALFORMED_PAYLOAD=REJECTED")


if __name__ == "__main__":
    main()
