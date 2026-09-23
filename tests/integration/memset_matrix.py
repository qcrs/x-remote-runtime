#!/usr/bin/env python3
import ctypes
import os
from pathlib import Path

SUCCESS = 0
INVALID_VALUE = 1
INVALID_DEVICE_POINTER = 17
NOT_READY = 600
H2D, D2H = 1, 2


def check(name, actual, expected):
    if actual != expected:
        raise RuntimeError(f"{name}: actual={actual} expected={expected}")
    print(f"M3_S2_CHECK={name}:PASS")


def main():
    root = Path(__file__).resolve().parents[2]
    lib = ctypes.CDLL(str(root / "dist/lib/libcorex_remote_cudart.so.1.1.0"))
    lib.cudaMalloc.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_size_t]
    lib.cudaMalloc.restype = ctypes.c_int
    lib.cudaMemset.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_size_t]
    lib.cudaMemset.restype = ctypes.c_int
    lib.cudaMemsetAsync.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_size_t, ctypes.c_void_p]
    lib.cudaMemsetAsync.restype = ctypes.c_int
    lib.cudaMemcpy.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
    lib.cudaMemcpy.restype = ctypes.c_int
    lib.cudaDeviceSynchronize.restype = ctypes.c_int
    lib.cudaStreamCreate.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    lib.cudaStreamCreate.restype = ctypes.c_int
    lib.cudaStreamSynchronize.argtypes = [ctypes.c_void_p]
    lib.cudaStreamSynchronize.restype = ctypes.c_int
    lib.cudaStreamDestroy.argtypes = [ctypes.c_void_p]
    lib.cudaStreamDestroy.restype = ctypes.c_int
    lib.cudaFree.argtypes = [ctypes.c_void_p]
    lib.cudaFree.restype = ctypes.c_int
    ptr = ctypes.c_void_p()
    check("alloc", lib.cudaMalloc(ctypes.byref(ptr), 64), SUCCESS)
    output = (ctypes.c_ubyte * 64)()
    check("sync-nonzero", lib.cudaMemset(ptr, 0xAB, 64), SUCCESS)
    check("read-sync", lib.cudaMemcpy(output, ptr, 64, D2H), SUCCESS)
    if list(output) != [0xAB] * 64:
        raise RuntimeError("sync memset data mismatch")
    check("clear", lib.cudaMemset(ptr, 0, 64), SUCCESS)
    check("offset", lib.cudaMemset(ctypes.c_void_p(ptr.value + 8), 0x7A, 8), SUCCESS)
    check("read-offset", lib.cudaMemcpy(output, ptr, 64, D2H), SUCCESS)
    if list(output) != [0] * 8 + [0x7A] * 8 + [0] * 48:
        raise RuntimeError("offset memset data mismatch")
    check("async-nonzero", lib.cudaMemsetAsync(ptr, 0xCD, 32, None), SUCCESS)
    check("device-sync", lib.cudaDeviceSynchronize(), SUCCESS)
    check("read-async", lib.cudaMemcpy(output, ptr, 32, D2H), SUCCESS)
    if list(output[:32]) != [0xCD] * 32:
        raise RuntimeError("async memset data mismatch")
    stream = ctypes.c_void_p()
    check("stream-create", lib.cudaStreamCreate(ctypes.byref(stream)), SUCCESS)
    check("async-explicit-stream", lib.cudaMemsetAsync(ptr, 0xEF, 16, stream), SUCCESS)
    check("stream-sync", lib.cudaStreamSynchronize(stream), SUCCESS)
    check("read-explicit-stream", lib.cudaMemcpy(output, ptr, 16, D2H), SUCCESS)
    if list(output[:16]) != [0xEF] * 16:
        raise RuntimeError("explicit stream memset data mismatch")
    check("stream-destroy", lib.cudaStreamDestroy(stream), SUCCESS)
    check("zero-byte-null", lib.cudaMemset(None, 0, 0), SUCCESS)
    check("bounds", lib.cudaMemset(ptr, 1, 65), INVALID_VALUE)
    check("invalid-pointer", lib.cudaMemset(ctypes.c_void_p(0x1234), 1, 1), INVALID_DEVICE_POINTER)
    check("free", lib.cudaFree(ptr), SUCCESS)
    print("M3_S2_MEMSET_MATRIX=PASS")


if __name__ == "__main__":
    main()
