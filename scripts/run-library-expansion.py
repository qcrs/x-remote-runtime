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
work = root / "build/library-expansion"
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
run(common + [f"-I{corex}/include", root / "tests/probes/corex_library_probe.c",
              f"-L{corex}/lib64", f"-Wl,-rpath,{corex}/lib64", "-lcuda", "-o", probe], "driver-probe-compile")
image=work / "library_kernels.cubin"
gcc_version=subprocess.check_output(["g++", "-dumpfullversion"],text=True).split(".")[0]
includes=[]
for directory in [f"/usr/include/c++/{gcc_version}",f"/usr/include/aarch64-linux-gnu/c++/{gcc_version}",f"/usr/include/c++/{gcc_version}/backward"]:
    if Path(directory).exists():includes += ["-isystem",directory]
run([corex / "bin/clang++", "-x", "ivcore", f"--cuda-path={corex}", "--cuda-device-only", f"-I{corex}/include"] + includes +
    [root / "tests/integration/library_kernels.cu", "-o",image],"device-image-compile")
run([probe, image], "driver-probe")
runtime_probe=work / "runtime-probe"
run(common + [f"-I{corex}/include",root / "tests/probes/corex_library_runtime_probe.c",
              f"-L{corex}/lib64",f"-Wl,-rpath,{corex}/lib64","-lcudart","-o",runtime_probe],"runtime-probe-compile")
run([runtime_probe,image],"runtime-probe")
binary = work / "library_expansion_app"
run(common + ["-Wno-deprecated-declarations", f"-I{corex}/include", root / "tests/integration/library_expansion.c",
              f"-L{root}/dist/lib", "-lcorex_remote_cudart", f"-Wl,-rpath,{root}/dist/lib",
              "-o", binary], "public-client-compile")
handles_object=work / "kernel-handles.o"
run([corex / "bin/clang++","-x","ivcore",f"--cuda-path={corex}",f"-I{corex}/include","-DCOREX_REMOTE_HANDLES_TEST"] + includes +
    ["-c",root / "tests/probes/corex_kernel_handles_probe.cu","-o",handles_object],"compiler-handles-compile")
handles_binary=work / "kernel-handles-app"
run(["g++",handles_object,f"-L{root}/dist/lib","-lcorex_remote_cudart",f"-Wl,-rpath,{root}/dist/lib","-o",handles_binary],"compiler-handles-link")
legacy_probe=work / "legacy-probe"
run(common + [f"-I{corex}/include",root / "tests/probes/corex_legacy_control_probe.c",
              f"-L{corex}/lib64",f"-Wl,-rpath,{corex}/lib64","-lcudart","-o",legacy_probe],"legacy-probe-compile")
run([legacy_probe],"legacy-control-probe")
legacy_binary=work / "legacy-control-app"
run(common + [f"-I{corex}/include",root / "tests/integration/legacy_control.c",
              f"-L{root}/dist/lib","-lcorex_remote_cudart",f"-Wl,-rpath,{root}/dist/lib","-o",legacy_binary],"legacy-client-compile")
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
        run([handles_binary],"compiler-kernel-identities",env)
        run([legacy_binary],"legacy-control-integration",env)
        lifecycle_env=env.copy()
        lifecycle_env["OUT"]=str(root / "build/library-expansion/lifecycle")
        run([root / "scripts/run-lifecycle-integration.sh"],"lifecycle",lifecycle_env)
        run(["python3", root / "tests/integration/library_expansion_protocol.py", image], "protocol-integration", env)
    finally:
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
print("LIBRARY_EXPANSION_RUN=PASS", flush=True)
