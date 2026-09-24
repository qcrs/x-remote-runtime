#!/usr/bin/env python3
import ctypes
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[2]
    lib = ctypes.CDLL(str(root / "dist/lib/libcorex_remote_cudart.so.1.1.0"))
    lib.cudaDriverGetVersion.argtypes = [ctypes.POINTER(ctypes.c_int)]
    lib.cudaRuntimeGetVersion.argtypes = [ctypes.POINTER(ctypes.c_int)]
    driver = ctypes.c_int(); runtime = ctypes.c_int()
    if lib.cudaDriverGetVersion(ctypes.byref(driver)) != 0 or driver.value <= 0:
        raise RuntimeError("driver version query failed")
    if lib.cudaRuntimeGetVersion(ctypes.byref(runtime)) != 0 or runtime.value != 11000:
        raise RuntimeError(f"runtime version query failed: {runtime.value}")
    print(f"M3_S4_VERSION_QUERIES=PASS driver={driver.value} runtime={runtime.value}")


if __name__ == "__main__": main()
