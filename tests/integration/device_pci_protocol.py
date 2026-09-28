#!/usr/bin/env python3
import os
from pathlib import Path
import socket
import struct
import subprocess

from session_failure_reconnect import start_server, stop_server

MAGIC = 0x43525839
VERSION = 3
ST_OK = 0
ST_BAD_REQUEST = 1
ST_INVALID_DEVICE = 8
OP_CLOSE = 7
OP_DEVICE_GET_PCI_BUS_ID = 47
OP_DEVICE_GET_BY_PCI_BUS_ID = 48


def receive_exact(connection, length):
    data = bytearray()
    while len(data) < length:
        chunk = connection.recv(length - len(data))
        if not chunk:
            raise RuntimeError("unexpected EOF")
        data.extend(chunk)
    return bytes(data)


def request(connection, opcode, request_id, payload=b""):
    connection.sendall(
        struct.pack("!IIIII", MAGIC, VERSION, opcode, request_id, len(payload))
        + payload
    )
    header = struct.unpack("!IIIIII", receive_exact(connection, 24))
    body = receive_exact(connection, header[5])
    if header[:4] != (MAGIC, VERSION, opcode, request_id):
        raise RuntimeError(f"response identity mismatch: {header}")
    return header[4], body


def expect(connection, opcode, request_id, payload, status, body_length=0):
    actual_status, body = request(connection, opcode, request_id, payload)
    if actual_status != status or len(body) != body_length:
        raise RuntimeError(
            f"opcode={opcode} status={actual_status} body={len(body)}"
        )
    return body


def main():
    root = Path(__file__).resolve().parents[2]
    output = Path(os.environ.get("OUT", root / "evidence/m3/s7/device-pci"))
    output.mkdir(parents=True, exist_ok=True)
    port = int(os.environ.get("M3_S7_TEST_PORT", "50080"))
    env = os.environ.copy()
    env["COREX_REMOTE_HOST"] = "127.0.0.1"
    env["COREX_REMOTE_PORT"] = str(port)
    env["LD_LIBRARY_PATH"] = str(root / "dist/lib") + os.pathsep + env.get(
        "LD_LIBRARY_PATH", ""
    )
    server, log = start_server(
        str(root / "dist/bin/runtime_server"), port, output / "server.log"
    )
    try:
        client = subprocess.run(
            [str(root / "dist/bin/device_pci_app")],
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        (output / "client.stdout.log").write_text(client.stdout)
        (output / "client.stderr.log").write_text(client.stderr)
        if client.returncode != 0 or "M3_S7_DEVICE_PCI=PASS " not in client.stdout:
            raise RuntimeError("PCI client integration failed")

        with socket.create_connection(("127.0.0.1", port), timeout=10) as connection:
            expect(connection, OP_DEVICE_GET_PCI_BUS_ID, 1, b"", ST_BAD_REQUEST)
            expect(connection, OP_DEVICE_GET_BY_PCI_BUS_ID, 2,
                   struct.pack("!I", 12), ST_BAD_REQUEST)
            body = expect(connection, OP_DEVICE_GET_PCI_BUS_ID, 3,
                          struct.pack("!i", 0), ST_OK, 16)
            length = struct.unpack("!I", body[:4])[0]
            if body[4:] != b"0000:81:00.0" or length != 12:
                raise RuntimeError("bounded PCI response bytes mismatch")
            body = expect(connection, OP_DEVICE_GET_BY_PCI_BUS_ID, 4,
                          struct.pack("!I", 12) + b"0000:81:00.0", ST_OK, 4)
            if struct.unpack("!i", body)[0] != 0:
                raise RuntimeError("PCI reverse lookup mismatch")
            expect(connection, OP_DEVICE_GET_BY_PCI_BUS_ID, 5,
                   struct.pack("!I", 12) + b"0000:ff:ff.0",
                   ST_INVALID_DEVICE)
            expect(connection, OP_DEVICE_GET_BY_PCI_BUS_ID, 6,
                   struct.pack("!I", 0), ST_BAD_REQUEST)
            expect(connection, OP_DEVICE_GET_BY_PCI_BUS_ID, 7,
                   struct.pack("!I", 12) + b"0000\x00:ff:ff.0",
                   ST_BAD_REQUEST)
            expect(connection, OP_DEVICE_GET_BY_PCI_BUS_ID, 8,
                   struct.pack("!I", 33) + b"a" * 33, ST_BAD_REQUEST)
            expect(connection, OP_DEVICE_GET_BY_PCI_BUS_ID, 9,
                   struct.pack("!I", 12), ST_BAD_REQUEST)
            request(connection, OP_CLOSE, 10)

        (output / "00-RESULTS.txt").write_text(
            "COREX_PCI_ROUND_TRIP=PASS\n"
            "CLIENT_INVALID_ARGUMENTS=PASS\n"
            "BOUNDED_STRING_WIRE_NEGATIVES=PASS\n"
            "UNKNOWN_PCI_ID=ST_INVALID_DEVICE\n"
            "RESULT=PASS\n"
        )
        print("M3_S7_DEVICE_PCI_INTEGRATION=PASS")
    finally:
        stop_server(server, log)


if __name__ == "__main__":
    main()
