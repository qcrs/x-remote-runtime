#!/usr/bin/env python3
"""Namespace public-library failure/reset checks; control FD only coordinates kill."""
import ctypes as C
import os
from pathlib import Path
import socket
import sys

root = Path(__file__).resolve().parents[2]
runtime = C.CDLL(str(root / 'dist/lib/libcorex_remote_cudart.so.1.1.0'))
ptr = C.c_void_p
signatures = {
    'cudaGetDeviceCount': [C.POINTER(C.c_int)], 'cudaDeviceSynchronize': [], 'cudaDeviceReset': [],
    'cudaMalloc': [C.POINTER(ptr), C.c_size_t], 'cudaFree': [ptr],
    'cudaStreamCreate': [C.POINTER(ptr)], 'cudaStreamQuery': [ptr],
    'cudaEventCreate': [C.POINTER(ptr)], 'cudaEventQuery': [ptr],
    'cudaDeviceGetDefaultMemPool': [C.POINTER(ptr), C.c_int], 'cudaMemPoolTrimTo': [ptr, C.c_size_t],
    'cudaMallocAsync': [C.POINTER(ptr), C.c_size_t, ptr],
    'cudaMemcpyAsync': [ptr, ptr, C.c_size_t, C.c_int, ptr],
    'cudaMemcpy': [ptr, ptr, C.c_size_t, C.c_int], 'cudaMemsetAsync': [ptr, C.c_int, C.c_size_t, ptr],
    'cudaGraphCreate': [C.POINTER(ptr), C.c_uint],
    'cudaGraphInstantiate': [C.POINTER(ptr), ptr, ptr, ptr, C.c_size_t],
    'cudaGraphDestroy': [ptr], 'cudaGraphLaunch': [ptr, ptr],
    'cudaLibraryLoadFromFile': [C.POINTER(ptr), C.c_char_p, ptr, ptr, C.c_uint, ptr, ptr, C.c_uint],
}
for name, types in signatures.items():
    getattr(runtime, name).argtypes = types
    getattr(runtime, name).restype = C.c_int


def check(name, result, want=0):
    print(f'{name}={result} expected={want}', flush=True)
    assert result == want, name


def resources():
    allocation, stream, event, pool, graph, executable, module = [ptr() for _ in range(7)]
    check('create-stream', runtime.cudaStreamCreate(C.byref(stream)))
    check('create-event', runtime.cudaEventCreate(C.byref(event)))
    check('default-pool', runtime.cudaDeviceGetDefaultMemPool(C.byref(pool), 0))
    check('async-allocation', runtime.cudaMallocAsync(C.byref(allocation), 64, stream))
    check('fill', runtime.cudaMemsetAsync(allocation, 0x57, 64, stream))
    check('create-graph', runtime.cudaGraphCreate(C.byref(graph), 0))
    check('instantiate', runtime.cudaGraphInstantiate(C.byref(executable), graph, None, None, 0))
    check('module', runtime.cudaLibraryLoadFromFile(C.byref(module),
          os.fsencode(root / 'build/session_lifecycle.cubin'), None, None, 0, None, None, 0))
    host = (C.c_ubyte * 64)(*([0x19] * 64))
    check('pending-host-transfer', runtime.cudaMemcpyAsync(host, allocation, 64, 2, stream))
    assert bytes(host) == bytes([0x19] * 64), 'host transfer became visible without retirement'
    return (allocation, stream, event, pool, graph, executable), host


def stale(objects):
    allocation, stream, event, pool, graph, executable = objects
    check('stale-AllocationID', runtime.cudaFree(allocation), 17)
    check('stale-StreamID', runtime.cudaStreamQuery(stream), 400)
    check('stale-EventID', runtime.cudaEventQuery(event), 400)
    check('stale-PoolID', runtime.cudaMemPoolTrimTo(pool, 0), 400)
    check('stale-GraphID', runtime.cudaGraphDestroy(graph), 400)
    check('stale-GraphExecID', runtime.cudaGraphLaunch(executable, None), 400)


if len(sys.argv) == 2 and sys.argv[1] == '--exit-live':
    objects, host = resources()
    print('CLIENT_ABRUPT_EXIT_WITH_LIVE_RESOURCES=PASS', flush=True)
    os._exit(0)  # Deliberately skip Runtime atexit shutdown; server must observe EOF.

control = socket.socket(fileno=int(sys.argv[1]))
old, host = resources()
control.sendall(b'kill')
assert control.recv(32) == b'restarted'
check('dead-session-observed', runtime.cudaDeviceSynchronize(), 999)
assert bytes(host) == bytes([0x19] * 64), 'failed pending D2H wrote host storage'
check('fresh-generation', runtime.cudaGetDeviceCount(C.byref(C.c_int())))
stale(old)
fresh, reset_host = resources()
assert all(a.value != b.value for a, b in zip(old, fresh)), 'client tombstones reused'
check('reset-with-pending-transfer', runtime.cudaDeviceReset())
assert bytes(reset_host) == bytes([0x57] * 64), 'reset did not retire D2H'
stale(fresh)
check('fresh-after-reset', runtime.cudaGetDeviceCount(C.byref(C.c_int())))
allocation = ptr()
value, result = C.c_int(12345), C.c_int()
check('fresh-malloc', runtime.cudaMalloc(C.byref(allocation), 4))
check('fresh-H2D', runtime.cudaMemcpy(allocation, C.byref(value), 4, 1))
check('fresh-D2H', runtime.cudaMemcpy(C.byref(result), allocation, 4, 2))
assert result.value == value.value
check('fresh-free', runtime.cudaFree(allocation))
print('REMOTE_FAILURE_RESET=PASS all_six_ID_families pending_D2H fresh_generation', flush=True)
