#!/usr/bin/env python3
"""Grouped native and independent public graph/capture capability validation."""
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
parser.add_argument("--integration-only", action="store_true")
parser.add_argument("--probes-only", action="store_true")
parser.add_argument("--expansion", action="store_true", help="validate the post-0383270 expansion")
parser.add_argument("--ownership-checks", action="store_true", help="include lifecycle and session isolation")
args = parser.parse_args()
if args.integration_only and args.probes_only:
    parser.error("--integration-only and --probes-only are mutually exclusive")
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
work = root / "build/graphs"
work.mkdir(parents=True, exist_ok=True)
corex = Path(os.environ.get("COREX", "/usr/local/corex-4.4.0"))
commands = (out / "COMMANDS.txt").read_text().splitlines() if (out / "COMMANDS.txt").exists() else []


def run(command, label, env=None):
    commands.append(" ".join(map(str, command)))
    (out / "COMMANDS.txt").write_text("\n".join(commands) + "\n")
    with (out / (label + ".stdout.log")).open("w") as stdout, (out / (label + ".stderr.log")).open("w") as stderr:
        result = subprocess.run(list(map(str, command)), cwd=root, env=env, stdout=stdout, stderr=stderr, timeout=90)
    if result.returncode:
        raise RuntimeError(f"{label} failed ({result.returncode}); see {out}")
    print(label + "=PASS", flush=True)


validation_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
baseline_file=out / "BASE_HEAD"
baseline_head=baseline_file.read_text().strip() if baseline_file.exists() else validation_head
(out / "ENVIRONMENT.txt").write_text(
    f"UTC={time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}\n"
    f"HOST={platform.node()}\nPLATFORM={platform.platform()}\nCOREX={corex}\n"
    f"BASE_HEAD={baseline_head}\nVALIDATION_HEAD={validation_head}\n"
    "TOPOLOGY=independent client -> 127.0.0.1 socket -> CRX9/V3 -> server -> CoreX Driver\n"
)
cc = os.environ.get("CC", "gcc")
common = [cc, "-O2", "-Wall", "-Wextra", "-Werror", "-std=gnu11"]
image = root / "build/session_lifecycle.cubin"
if not args.integration_only:
    probes=[("corex_graph_probe.c", "driver-probe", "cuda"),
                               ("corex_graph_runtime_probe.c", "runtime-probe", "cudart"),
                               ("corex_graph_execution_probe.c", "execution-probe", "cuda")]
    if args.expansion:
        probes=[("corex_graph_expansion_probe.c", "native-expansion", "cuda"),
                ("corex_child_graph_execution_probe.c", "native-child-execution", "cuda"),
                ("corex_graph_memory_probe.c", "native-memory", "cuda"),
                ("corex_graph_expansion_runtime_probe.c", "native-runtime-expansion", "cudart"),
                ("corex_graph_remaining_probe.c", "native-remaining", "cuda")]
    for source, label, lib in probes:
        binary = work / label
        run(common + [f"-I{corex}/include", root / "tests/probes" / source,
                      f"-L{corex}/lib64", f"-Wl,-rpath,{corex}/lib64", "-l" + lib, "-ldl","-pthread"]+
                      (["-lcudart"] if label=="native-remaining" else [])+["-o", binary], label + "-compile")
        run([binary] + ([image] if label in ("execution-probe","native-child-execution","native-memory") else []), label)
if args.probes_only:
    print("GRAPH_PROBES=PASS",flush=True)
    raise SystemExit(0)
binary = work / "graph-app"
run(common + [f"-I{corex}/include", root / "tests/integration/graphs.c",
              f"-L{root}/dist/lib", "-lcorex_remote_cudart", f"-Wl,-rpath,{root}/dist/lib", "-pthread", "-o", binary], "public-client-compile")
run(common + ["-DCOREX_GRAPH_INTERNAL_ABI", f"-I{root}/include/internal", "-c",
              root / "tests/integration/graphs.c", "-o", work / "graph-abi.o"], "internal-graph-abi")
if args.expansion:
    expansion_binary=work / "graph-expansion-app"
    run(common + [f"-I{corex}/include", root / "tests/integration/graph_expansion.c",
                  f"-L{root}/dist/lib", "-lcorex_remote_cudart", f"-Wl,-rpath,{root}/dist/lib", "-pthread", "-o", expansion_binary], "expansion-client-compile")
    run(common + ["-DCOREX_GRAPH_INTERNAL_ABI", f"-I{root}/include/internal", "-c",
                  root / "tests/integration/graph_expansion.c", "-o", work / "graph-expansion-abi.o"], "expansion-abi")
    memory_binary=work / "graph-memory-app"
    run(common+[f"-I{corex}/include",root / "tests/integration/graph_memory.c",f"-L{root}/dist/lib",
                "-lcorex_remote_cudart",f"-Wl,-rpath,{root}/dist/lib","-o",memory_binary],"memory-client-compile")
    run(common+["-DCOREX_GRAPH_INTERNAL_ABI",f"-I{root}/include/internal","-c",root / "tests/integration/graph_memory.c",
                "-o",work / "graph-memory-abi.o"],"memory-abi")
    host_binary=work / "graph-host-app"
    run(common+[f"-I{corex}/include",root / "tests/integration/graph_host.c",f"-L{root}/dist/lib",
                "-lcorex_remote_cudart",f"-Wl,-rpath,{root}/dist/lib","-pthread","-o",host_binary],"host-client-compile")
    run(common+["-DCOREX_GRAPH_INTERNAL_ABI",f"-I{root}/include/internal","-c",root / "tests/integration/graph_host.c",
                "-o",work / "graph-host-abi.o"],"host-abi")
    control_binary=work / "graph-controls-app"
    run(common+[f"-I{corex}/include",root / "tests/integration/graph_controls.c",f"-L{root}/dist/lib",
                "-lcorex_remote_cudart",f"-Wl,-rpath,{root}/dist/lib","-pthread","-o",control_binary],"control-client-compile")
    run(common+["-DCOREX_GRAPH_INTERNAL_ABI",f"-I{root}/include/internal","-c",root / "tests/integration/graph_controls.c",
                "-o",work / "graph-controls-abi.o"],"control-abi")
env = os.environ.copy()
env["PYTHONDONTWRITEBYTECODE"] = "1"
env["COREX_REMOTE_HOST"] = "127.0.0.1"
if args.expansion:
    env["COREX_GRAPH_EXPANSION"]="1"
with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    env["COREX_REMOTE_PORT"] = str(reserve.getsockname()[1])
port = int(env["COREX_REMOTE_PORT"])
with (out / "server.stdout.log").open("w") as stdout, (out / "server.stderr.log").open("w") as stderr:
    server = subprocess.Popen([root / "dist/bin/runtime_server"], cwd=root, env=env, stdout=stdout, stderr=stderr)
    commands.append(f"COREX_REMOTE_PORT={port} dist/bin/runtime_server # independent PID={server.pid}")
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
        if args.expansion:
            run([expansion_binary, image], "expansion-integration", env)
            run([memory_binary,image],"memory-integration",env)
            run([host_binary],"host-integration",env)
            run([control_binary,out / "client-graph.dot"],"control-integration",env)
        run(["python3", root / "tests/integration/graphs_protocol.py"], "protocol-integration", env)
        if not args.integration_only or args.ownership_checks:
            lifecycle_env=env.copy();lifecycle_env["OUT"]=str(out / "lifecycle")
            run([root / "scripts/run-lifecycle-integration.sh"], "lifecycle", lifecycle_env)
            isolation_env=env.copy();isolation_env["OUT"]=str(out / "session-isolation")
            run([root / "scripts/run-server-session-isolation.sh"], "session-isolation", isolation_env)
            if not args.expansion:
                baseline_env=env.copy();baseline_env["OUT"]=str(out / "baseline")
                run([root / "scripts/run-integration.sh"], "baseline", baseline_env)
    finally:
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
print("GRAPHS_RUN=PASS", flush=True)
