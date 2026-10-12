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
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
work = root / "build/execution-fill"
work.mkdir(parents=True, exist_ok=True)
corex = Path(os.environ.get("COREX", "/usr/local/corex-4.4.0"))
commands = []


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
probe=work / "driver-probe"
run(common + [f"-I{corex}/include", root / "tests/probes/corex_execution_fill_probe.c",
              f"-L{corex}/lib64", f"-Wl,-rpath,{corex}/lib64", "-lcuda", "-o", probe], "driver-probe-compile")
run([probe, root / "build/session_lifecycle.cubin"], "driver-probe")
binary = work / "execution_fill_app"
run(common + ["-Wno-deprecated-declarations", f"-I{corex}/include", root / "tests/integration/execution_fill.c",
              f"-L{root}/dist/lib", "-lcorex_remote_cudart", f"-Wl,-rpath,{root}/dist/lib",
              "-o", binary], "public-client-compile")
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
        run([binary, root / "build/session_lifecycle.cubin"], "integration", env)
        run(["python3", root / "tests/integration/execution_fill_protocol.py"], "protocol-integration", env)
    finally:
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
print("EXECUTION_FILL_RUN=PASS", flush=True)
