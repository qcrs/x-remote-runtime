#!/usr/bin/env python3
import ctypes
import os
from pathlib import Path

SUCCESS = 0
INVALID_VALUE = 1
INVALID_DEVICE_POINTER = 17
NOT_SUPPORTED = 801
H2H, H2D, D2H, D2D, DEFAULT = range(5)


def check(name, actual, expected):
    if actual != expected:
        raise RuntimeError(f"{name}: actual={actual} expected={expected}")
    print(f"M3_S1_CHECK={name}:PASS")


def main():
    root = Path(__file__).resolve().parents[2]
    lib = ctypes.CDLL(str(root / "dist/lib/libcorex_remote_cudart.so.1.1.0"))
    lib.cudaMalloc.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_size_t]
    lib.cudaMemcpy.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
    lib.cudaMemcpyAsync.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_void_p]
    lib.cudaMemcpyAsync.restype = ctypes.c_int
    lib.cudaFree.argtypes = [ctypes.c_void_p]
    a, b = ctypes.c_void_p(), ctypes.c_void_p()
    check("alloc-a", lib.cudaMalloc(ctypes.byref(a), 32), SUCCESS)
    check("alloc-b", lib.cudaMalloc(ctypes.byref(b), 32), SUCCESS)
    source = (ctypes.c_ubyte * 32)(*range(32))
    output = (ctypes.c_ubyte * 32)()
    check("h2d", lib.cudaMemcpy(a, source, 32, H2D), SUCCESS)
    check("d2d", lib.cudaMemcpy(b, a, 32, D2D), SUCCESS)
    check("d2h", lib.cudaMemcpy(output, b, 32, D2H), SUCCESS)
    if list(output) != list(source):
        raise RuntimeError("D2D data mismatch")
    ctypes.memset(output, 0, 32)
    check("default-d2d", lib.cudaMemcpy(b, a, 32, DEFAULT), SUCCESS)
    check("default-d2h", lib.cudaMemcpy(output, b, 32, DEFAULT), SUCCESS)
    if list(output) != list(source):
        raise RuntimeError("default D2D data mismatch")
    host_a = (ctypes.c_ubyte * 8)(*range(8))
    host_b = (ctypes.c_ubyte * 8)()
    check("h2h", lib.cudaMemcpy(host_b, host_a, 8, H2H), SUCCESS)
    if list(host_a) != list(host_b):
        raise RuntimeError("H2H data mismatch")
    check("d2d-bounds", lib.cudaMemcpy(b, a, 33, D2D), INVALID_VALUE)
    check("d2d-overlap", lib.cudaMemcpy(a, ctypes.c_void_p(a.value + 1), 8, D2D), INVALID_VALUE)
    check("async-d2d-explicit", lib.cudaMemcpyAsync(b, a, 8, D2D, None), NOT_SUPPORTED)
    check("free-a", lib.cudaFree(a), SUCCESS)
    check("free-b", lib.cudaFree(b), SUCCESS)
    print("M3_S1_MEMCPY_MATRIX=PASS")


if __name__ == "__main__":
    main()
