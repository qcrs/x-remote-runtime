#!/usr/bin/env python3
"""Build and measure one compatibility batch, preserving logs rather than binaries."""
import argparse
import os
from pathlib import Path
import platform
import socket
import subprocess
import time

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--out", type=Path, required=True)
parser.add_argument("--integration-only", action="store_true", help="Rerun the public/wire family after local API changes; retain prior grouped probes and ownership regression evidence")
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
work = root / "build/memory-pools"
work.mkdir(parents=True, exist_ok=True)
corex = Path(os.environ.get("COREX", "/usr/local/corex-4.4.0"))
commands = (out / "COMMANDS.txt").read_text().splitlines() if args.integration_only and (out / "COMMANDS.txt").exists() else []


def run(command, label, env=None):
    commands.append(" ".join(map(str, command)))
    (out / "COMMANDS.txt").write_text("\n".join(commands) + "\n")
    with (out / (label + ".stdout.log")).open("w") as stdout, (out / (label + ".stderr.log")).open("w") as stderr:
        result = subprocess.run(list(map(str, command)), cwd=root, env=env, stdout=stdout, stderr=stderr)
    if result.returncode:
        raise RuntimeError(f"{label} failed ({result.returncode}); see {out}")
    print(label + "=PASS", flush=True)


(out / "ENVIRONMENT.txt").write_text(
    f"UTC={time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}\n"
    f"HOST={platform.node()}\nPLATFORM={platform.platform()}\nCOREX={corex}\n"
    f"BASE_HEAD={subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()}\n"
    "TOPOLOGY=independent client -> 127.0.0.1 socket -> CRX9/V3 -> server -> CoreX Driver\n"
)
cc = os.environ.get("CC", "gcc")
common = [cc, "-O2", "-Wall", "-Wextra", "-Werror", "-std=gnu11"]
image = root / "build/session_lifecycle.cubin"
entry_source = root / "tests/integration/driver_entrypoints.c"
run(common + ["-DCOREX_ENTRY_NATIVE_PROBE", f"-I{corex}/include", entry_source,
              f"-L{corex}/lib64", f"-Wl,-rpath,{corex}/lib64", "-lcudart", "-o",work / "entry-native"],"entry-native-compile")
run([work / "entry-native"],"entry-native")
entry_binary = work / "entry-remote"
run(common + [f"-I{corex}/include",entry_source,f"-L{root}/dist/lib","-lcorex_remote_cudart",
              f"-Wl,-rpath,{root}/dist/lib","-o",entry_binary],"entry-public-compile")
if not args.integration_only:
    run(common + [f"-I{corex}/include",root / "tests/probes/corex_pool_runtime_probe.c",
                  f"-L{corex}/lib64",f"-Wl,-rpath,{corex}/lib64","-lcudart","-o",work / "runtime-probe"],"runtime-probe-compile")
    run([work / "runtime-probe"],"runtime-probe")
    run(common + [f"-I{corex}/include",root / "tests/probes/corex_pool_probe.c",
                  f"-L{corex}/lib64",f"-Wl,-rpath,{corex}/lib64","-lcuda","-o",work / "pool-probe"],"driver-probe-compile")
    run([work / "pool-probe"],"driver-probe")
binary = work / "memory_pools_app"
run(common + ["-Wno-deprecated-declarations", f"-I{corex}/include", root / "tests/integration/memory_pools.c",
              f"-L{root}/dist/lib", "-lcorex_remote_cudart", f"-Wl,-rpath,{root}/dist/lib",
              "-pthread", "-o", binary], "public-client-compile")
run(common + ["-DCOREX_POOL_INTERNAL_ABI", f"-I{root}/include/internal", "-c",
              root / "tests/integration/memory_pools.c", "-o", work / "pool-abi.o"],"internal-pool-abi")
env = os.environ.copy()
env["PYTHONDONTWRITEBYTECODE"] = "1"
env["COREX_REMOTE_HOST"] = "127.0.0.1"
if "COREX_REMOTE_PORT" not in env:
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        env["COREX_REMOTE_PORT"] = str(reserve.getsockname()[1])
port = int(env["COREX_REMOTE_PORT"])
with (out / "server.stdout.log").open("w") as stdout, (out / "server.stderr.log").open("w") as stderr:
    server = subprocess.Popen([root / "dist/bin/runtime_server"], cwd=root, env=env, stdout=stdout, stderr=stderr)
    commands.append(f"COREX_REMOTE_PORT={port} dist/bin/runtime_server  # independent Popen PID={server.pid}")
    try:
        for _ in range(200):
            if server.poll() is not None:
                raise RuntimeError("server exited before listening")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.05)
        else:
            raise RuntimeError("server readiness timeout")
        with (out / "ENVIRONMENT.txt").open("a") as environment:
            environment.write(f"PORT={port}\nSERVER_PID={server.pid}\n")
        run([binary, image], "integration", env)
        run([entry_binary], "entry-integration", env)
        run(["python3",root / "tests/integration/callback_errors.py"],"callback-errors",env)
        run(["python3",root / "tests/integration/memory_pools_protocol.py"],"protocol-integration",env)
        if not args.integration_only:
            lifecycle_env = env.copy()
            lifecycle_env["OUT"] = str(work / "lifecycle")
            run([root / "scripts/run-lifecycle-integration.sh"], "lifecycle", lifecycle_env)
            run([root / "scripts/run-server-session-isolation.sh"], "session-isolation", lifecycle_env)
            failure_env=env.copy();failure_env["OUT"]=str(work / "failure-reconnect")
            run([root / "scripts/run-failure-reconnect-integration.sh"],"failure-reconnect",failure_env)
    finally:
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
print("MEMORY_POOLS_RUN=PASS", flush=True)
