#!/usr/bin/env python3
"""Negative public-client contracts using a protocol peer with failed barriers."""
import ctypes
import os
from pathlib import Path
import socket
import struct
import threading

listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen(1)
os.environ['COREX_REMOTE_PORT'] = str(listener.getsockname()[1])
os.environ['COREX_REMOTE_HOST'] = '127.0.0.1'
requests = []


def read_exact(connection, size):
    data = b''
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise EOFError()
        data += chunk
    return data


def serve():
    connection, _ = listener.accept()
    with connection:
        while True:
            _, _, op, req, length = struct.unpack('!IIIII', read_exact(connection, 20))
            read_exact(connection, length)
            requests.append(op)
            status, body = 0, b''
            if op == 27:  # legacy peer, no pool capability advertised
                status = 1
            elif op == 12:
                body = struct.pack('!Q', 1)
            elif op == 15:  # fail stream completion
                status = 3
            elif op == 14:
                body = struct.pack('!I', 0)
            connection.sendall(struct.pack('!IIIIII', 0x43525839, 3, op, req, status, len(body)) + body)
            if op == 7:
                break
    listener.close()


thread = threading.Thread(target=serve, daemon=True)
thread.start()
runtime = ctypes.CDLL(str(Path(__file__).resolve().parents[2] / 'dist/lib/libcorex_remote_cudart.so.1'))
runtime.cudaStreamCreate.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
runtime.cudaStreamQuery.argtypes = [ctypes.c_void_p]
host_type = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
callback_type = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p)
runtime.cudaLaunchHostFunc.argtypes = [ctypes.c_void_p, host_type, ctypes.c_void_p]
runtime.cudaStreamAddCallback.argtypes = [ctypes.c_void_p, callback_type, ctypes.c_void_p, ctypes.c_uint]
runtime.cudaDeviceGetDefaultMemPool.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_int]
stream = ctypes.c_void_p()
assert runtime.cudaStreamCreate(ctypes.byref(stream)) == 0
observed = []
host = host_type(lambda data: observed.append('host'))
callback = callback_type(lambda s, status, data: observed.append((s, status)))
assert runtime.cudaLaunchHostFunc(stream, host, None) == 0
assert runtime.cudaStreamQuery(stream) == 0
assert observed == []  # host functions skip a failed completion
assert runtime.cudaStreamAddCallback(stream, callback, None, 0) == 0
assert runtime.cudaStreamQuery(stream) == 0
assert observed == [(stream.value, 999)]  # exactly once, with failure status
pool = ctypes.c_void_p()
assert runtime.cudaDeviceGetDefaultMemPool(ctypes.byref(pool), 0) == 801
assert 75 not in requests  # no pool RPC to a legacy peer
assert runtime.cudaDeviceReset() == 0
thread.join(timeout=5)
assert not thread.is_alive()
print('CALLBACK_ERRORS=PASS host_skipped callback_once_error legacy_pool_capability_gate')
