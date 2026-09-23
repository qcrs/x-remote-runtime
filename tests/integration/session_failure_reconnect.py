#!/usr/bin/env python3
import ctypes
import os
from pathlib import Path
import socket
import subprocess
import struct
import threading
import time

CUDA_SUCCESS = 0
CUDA_ERROR_INVALID_DEVICE_POINTER = 17
CUDA_ERROR_INVALID_RESOURCE_HANDLE = 400
CUDA_ERROR_UNKNOWN = 999
CUDA_ERROR_INITIALIZATION_ERROR = 3
CUDA_MEMCPY_HOST_TO_DEVICE = 1
CUDA_MEMCPY_DEVICE_TO_HOST = 2


def expect(name, actual, expected):
    result = "PASS" if actual == expected else "FAIL"
    print(f"M1_S5_CHECK name={name} actual={actual} expected={expected} result={result}")
    if actual != expected:
        raise RuntimeError(name)


def start_server(server_binary, port, log_path):
    log = open(log_path, "ab", buffering=0)
    env = os.environ.copy()
    env["COREX_REMOTE_PORT"] = str(port)
    process = subprocess.Popen(
        [server_binary], stdout=log, stderr=subprocess.STDOUT, env=env
    )
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited early: {process.returncode}")
        try:
            probe = socket.create_connection(("127.0.0.1", port), timeout=0.2)
            probe.close()
            return process, log
        except OSError:
            time.sleep(0.05)
    process.terminate()
    process.wait(timeout=5)
    log.close()
    raise RuntimeError("server did not become ready")


def stop_server(process, log):
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=10)
    log.close()


def configure_runtime(library):
    library.cudaGetDeviceCount.argtypes = [ctypes.POINTER(ctypes.c_int)]
    library.cudaGetDeviceCount.restype = ctypes.c_int
    library.cudaDeviceSynchronize.argtypes = []
    library.cudaDeviceSynchronize.restype = ctypes.c_int
    library.cudaMalloc.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_size_t]
    library.cudaMalloc.restype = ctypes.c_int
    library.cudaFree.argtypes = [ctypes.c_void_p]
    library.cudaFree.restype = ctypes.c_int
    library.cudaMemcpy.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_int,
    ]
    library.cudaMemcpy.restype = ctypes.c_int
    library.cudaStreamCreate.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    library.cudaStreamCreate.restype = ctypes.c_int
    library.cudaStreamQuery.argtypes = [ctypes.c_void_p]
    library.cudaStreamQuery.restype = ctypes.c_int
    library.cudaEventCreate.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    library.cudaEventCreate.restype = ctypes.c_int
    library.cudaEventQuery.argtypes = [ctypes.c_void_p]
    library.cudaEventQuery.restype = ctypes.c_int


def start_bad_protocol_server(port):
    ready = threading.Event()

    def run():
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", port))
        listener.listen(1)
        ready.set()
        connection, _ = listener.accept()
        header = b""
        while len(header) < 20:
            header += connection.recv(20 - len(header))
        _, _, opcode, request_id, payload_length = struct.unpack("!IIIII", header)
        remaining = payload_length
        while remaining:
            chunk = connection.recv(remaining)
            if not chunk:
                break
            remaining -= len(chunk)
        connection.sendall(
            struct.pack("!IIIIII", 0xDEADBEEF, 3, opcode, request_id, 0, 0)
        )
        connection.close()
        listener.close()

    thread = threading.Thread(target=run, daemon=True)
    thread.start()
    if not ready.wait(timeout=5):
        raise RuntimeError("bad protocol server did not start")
    return thread


def main():
    root = Path(__file__).resolve().parents[2]
    server_binary = str(root / "dist/bin/runtime_server")
    runtime_library = str(root / "dist/lib/libcorex_remote_cudart.so.1.1.0")
    output = Path(os.environ.get("OUT", root / "evidence/m1/s5/failure-reconnect"))
    output.mkdir(parents=True, exist_ok=True)
    port = int(os.environ.get("M1_S5_TEST_PORT", "50061"))
    os.environ["COREX_REMOTE_HOST"] = "127.0.0.1"
    os.environ["COREX_REMOTE_PORT"] = str(port)

    runtime = ctypes.CDLL(runtime_library)
    configure_runtime(runtime)
    server = None
    server_log = None

    try:
        server, server_log = start_server(
            server_binary, port, output / "server-generation-1.log"
        )
        count = ctypes.c_int()
        expect("initial-connect", runtime.cudaGetDeviceCount(ctypes.byref(count)), CUDA_SUCCESS)
        stop_server(server, server_log)
        server = server_log = None

        expect("idle-server-kill-failing-call", runtime.cudaDeviceSynchronize(), CUDA_ERROR_UNKNOWN)

        server, server_log = start_server(
            server_binary, port, output / "server-generation-2.log"
        )
        expect("reconnect-after-idle-kill", runtime.cudaGetDeviceCount(ctypes.byref(count)), CUDA_SUCCESS)

        old_pointer = ctypes.c_void_p()
        old_stream = ctypes.c_void_p()
        old_event = ctypes.c_void_p()
        expect("allocation-before-kill", runtime.cudaMalloc(ctypes.byref(old_pointer), 4), CUDA_SUCCESS)
        expect("stream-before-kill", runtime.cudaStreamCreate(ctypes.byref(old_stream)), CUDA_SUCCESS)
        expect("event-before-kill", runtime.cudaEventCreate(ctypes.byref(old_event)), CUDA_SUCCESS)

        stop_server(server, server_log)
        server = server_log = None
        expect("live-server-kill-failing-call", runtime.cudaDeviceSynchronize(), CUDA_ERROR_UNKNOWN)

        server, server_log = start_server(
            server_binary, port, output / "server-generation-3.log"
        )
        expect("reconnect-after-live-kill", runtime.cudaGetDeviceCount(ctypes.byref(count)), CUDA_SUCCESS)
        expect("failed-generation-pointer-stale", runtime.cudaFree(old_pointer), CUDA_ERROR_INVALID_DEVICE_POINTER)
        expect("failed-generation-stream-stale", runtime.cudaStreamQuery(old_stream), CUDA_ERROR_INVALID_RESOURCE_HANDLE)
        expect("failed-generation-event-stale", runtime.cudaEventQuery(old_event), CUDA_ERROR_INVALID_RESOURCE_HANDLE)

        fresh_pointer = ctypes.c_void_p()
        source = ctypes.c_int(12345)
        destination = ctypes.c_int(0)
        expect("fresh-allocation", runtime.cudaMalloc(ctypes.byref(fresh_pointer), 4), CUDA_SUCCESS)
        expect(
            "fresh-h2d",
            runtime.cudaMemcpy(
                fresh_pointer,
                ctypes.byref(source),
                4,
                CUDA_MEMCPY_HOST_TO_DEVICE,
            ),
            CUDA_SUCCESS,
        )
        expect(
            "fresh-d2h",
            runtime.cudaMemcpy(
                ctypes.byref(destination),
                fresh_pointer,
                4,
                CUDA_MEMCPY_DEVICE_TO_HOST,
            ),
            CUDA_SUCCESS,
        )
        if destination.value != source.value:
            raise RuntimeError("fresh generation roundtrip mismatch")
        expect("fresh-free", runtime.cudaFree(fresh_pointer), CUDA_SUCCESS)

        stop_server(server, server_log)
        server = server_log = None
        expect("pre-protocol-test-disconnect", runtime.cudaDeviceSynchronize(), CUDA_ERROR_UNKNOWN)
        bad_server = start_bad_protocol_server(port)
        expect("protocol-mismatch-during-hello", runtime.cudaGetDeviceCount(ctypes.byref(count)), CUDA_ERROR_INITIALIZATION_ERROR)
        bad_server.join(timeout=5)
        if bad_server.is_alive():
            raise RuntimeError("bad protocol server did not finish")

        server, server_log = start_server(
            server_binary, port, output / "server-generation-5.log"
        )
        expect("reconnect-after-protocol-mismatch", runtime.cudaGetDeviceCount(ctypes.byref(count)), CUDA_SUCCESS)
        print("M1_S5_FAILURE_RECONNECT_RESULT=PASS")
    finally:
        if server is not None:
            stop_server(server, server_log)


if __name__ == "__main__":
    main()
