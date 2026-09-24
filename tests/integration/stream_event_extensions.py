#!/usr/bin/env python3
import ctypes
from pathlib import Path

SUCCESS = 0
INVALID_VALUE = 1


def check(name, actual, expected=SUCCESS):
    if actual != expected:
        raise RuntimeError(f"{name}: actual={actual} expected={expected}")
    print(f"M3_S3_CHECK={name}:PASS")


def main():
    root = Path(__file__).resolve().parents[2]
    lib = ctypes.CDLL(str(root / "dist/lib/libcorex_remote_cudart.so.1.1.0"))
    stream = ctypes.c_void_p(); stream2 = ctypes.c_void_p()
    event1 = ctypes.c_void_p(); event2 = ctypes.c_void_p(); disabled = ctypes.c_void_p()
    flags = ctypes.c_uint(); priority = ctypes.c_int(); elapsed = ctypes.c_float()
    lib.cudaStreamCreateWithFlags.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_uint]
    lib.cudaStreamCreateWithPriority.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_uint, ctypes.c_int]
    lib.cudaStreamGetFlags.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint)]
    lib.cudaStreamGetPriority.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
    lib.cudaEventCreateWithFlags.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_uint]
    lib.cudaEventRecord.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    lib.cudaEventSynchronize.argtypes = [ctypes.c_void_p]
    lib.cudaEventElapsedTime.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_void_p, ctypes.c_void_p]
    lib.cudaStreamDestroy.argtypes = [ctypes.c_void_p]
    lib.cudaEventDestroy.argtypes = [ctypes.c_void_p]
    check("stream-nonblocking", lib.cudaStreamCreateWithFlags(ctypes.byref(stream), 1))
    check("stream-flags", lib.cudaStreamGetFlags(stream, ctypes.byref(flags)))
    if flags.value != 1: raise RuntimeError("flags round trip mismatch")
    check("stream-priority", lib.cudaStreamCreateWithPriority(ctypes.byref(stream2), 1, 0))
    check("priority-query", lib.cudaStreamGetPriority(stream2, ctypes.byref(priority)))
    check("event-start", lib.cudaEventCreateWithFlags(ctypes.byref(event1), 0))
    check("event-end", lib.cudaEventCreateWithFlags(ctypes.byref(event2), 0))
    check("event-disabled", lib.cudaEventCreateWithFlags(ctypes.byref(disabled), 2))
    check("record-start", lib.cudaEventRecord(event1, stream))
    check("record-end", lib.cudaEventRecord(event2, stream))
    check("sync-end", lib.cudaEventSynchronize(event2))
    check("elapsed", lib.cudaEventElapsedTime(ctypes.byref(elapsed), event1, event2))
    if elapsed.value < 0: raise RuntimeError("negative elapsed time")
    check("disabled-elapsed-rejected", lib.cudaEventElapsedTime(ctypes.byref(elapsed), disabled, event2), INVALID_VALUE)
    for obj in (event1, event2, disabled): check("event-destroy", lib.cudaEventDestroy(obj))
    for obj in (stream, stream2): check("stream-destroy", lib.cudaStreamDestroy(obj))
    print("M3_S3_STREAM_EVENT_EXTENSIONS=PASS")


if __name__ == "__main__": main()
