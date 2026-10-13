#!/usr/bin/env python3
"""M9 grouped native probes and independent public localhost CRX9 clients."""
import argparse
import os
from pathlib import Path
import shlex
import socket
import subprocess
import time

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, default=root / 'evidence/m9/partial-semantics-20261013')
args = p.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
work = root / 'build/m9'
work.mkdir(parents=True, exist_ok=True)
corex = Path(os.environ.get('COREX', '/usr/local/corex-4.4.0'))


def run(command, label, env=None):
    with (out / 'COMMANDS.txt').open('a') as f:
        f.write(shlex.join(map(str, command)) + '\n')
    with (out / (label + '.stdout.log')).open('w') as stdout, (out / (label + '.stderr.log')).open('w') as stderr:
        r = subprocess.run(list(map(str, command)), cwd=root, env=env,
                           stdout=stdout, stderr=stderr, timeout=60)
    (out / (label + '.exit-code.txt')).write_text(str(r.returncode) + '\n')
    if r.returncode:
        raise RuntimeError(f'{label} failed ({r.returncode}); see {out}')
    print(label + '=PASS', flush=True)


common = ['gcc', '-O2', '-Wall', '-Wextra', '-Werror', '-std=gnu11', '-Wno-deprecated-declarations', f'-I{corex}/include']
major = subprocess.check_output(['g++', '-dumpversion'], text=True).strip().split('.')[0]
includes = []
for directory in [f'/usr/include/c++/{major}', f'/usr/include/aarch64-linux-gnu/c++/{major}', f'/usr/include/c++/{major}/backward']:
    if Path(directory).exists():
        includes += ['-isystem', directory]
compiler = [corex / 'bin/clang++', '-x', 'ivcore', f'--cuda-path={corex}', f'-I{corex}/include', *includes]
image = work / 'library.cubin'
fatbin = work / 'library.fatbin'
obj = work / 'library.o'
run(compiler + ['--cuda-device-only', root / 'tests/integration/library_kernels.cu', '-o', image], 'library-image-compile')
run(compiler + ['-c', root / 'tests/integration/library_kernels.cu', '-o', obj], 'library-wrapper-compile')
run(['objcopy', '--dump-section', f'.nv_fatbin={fatbin}', obj], 'library-fatbin-extract')
for source, lib, label, arguments in [
    ('tests/probes/corex_partial_semantics_probe.c', 'cuda', 'native', [image]),
    ('tests/probes/corex_partial_library_probe.c', 'cuda', 'library-elf-native', [image]),
    ('tests/probes/corex_partial_library_probe.c', 'cuda', 'library-fatbin-native', [fatbin]),
    ('tests/probes/corex_partial_library_runtime_probe.c', 'cudart', 'library-runtime-native', [image]),
    ('tests/probes/corex_partial_driver_query_probe.c', 'cudart', 'driver-query-native', []),
    ('tests/integration/partial_driver_entries.c', 'cudart', 'driver-entries-native', []),
    ('tests/probes/corex_closure_function_probe.c', 'cuda', 'function-native', [image]),
]:
    binary = work / label
    run(common + [root / source, f'-L{corex}/lib64', f'-Wl,-rpath,{corex}/lib64', '-l' + lib, '-o', binary], label + '-compile')
    run([binary, *arguments], label)

# A valid PTX format is a prerequisite for implementing JIT semantics. CoreX's
# compiled-image options succeeding alone do not prove a working PTX compiler.
ptx = work / 'kernel.ptx'
ptx.write_text('.version 6.0\n.target sm_70\n.address_size 64\n.visible .entry m1s2_lifecycle_kernel() { ret; }\n')
run([work / 'library-elf-native', ptx], 'library-ptx-native')

clients = []
compiled = set()
for name, arguments in [
    ('driver_entrypoints', []), ('partial_driver_entries', []),
    ('partial_context_flags', []), ('library_expansion', [image]),
    ('library_expansion', [fatbin]), ('remaining_core', [image]),
]:
    binary = work / (name + '-remote')
    label = name + ('-fatbin' if arguments == [fatbin] else '')
    if name not in compiled:
        run(common + [root / 'tests/integration' / (name + '.c'), f'-L{root}/dist/lib',
                      f'-Wl,-rpath,{root}/dist/lib', '-lcorex_remote_cudart', '-pthread', '-o', binary], label + '-compile')
        compiled.add(name)
    clients.append(([binary, *arguments], label + '-remote'))

env = os.environ.copy()
env['COREX_REMOTE_BIND'] = env['COREX_REMOTE_HOST'] = '127.0.0.1'
env['PYTHONDONTWRITEBYTECODE'] = '1'
with socket.socket() as reserve:
    reserve.bind(('127.0.0.1', 0))
    env['COREX_REMOTE_PORT'] = str(reserve.getsockname()[1])
with (out / 'server.stdout.log').open('w') as stdout, (out / 'server.stderr.log').open('w') as stderr:
    server = subprocess.Popen([root / 'dist/bin/runtime_server'], cwd=root, env=env, stdout=stdout, stderr=stderr)
    with (out / 'COMMANDS.txt').open('a') as f:
        f.write(f"COREX_REMOTE_BIND=127.0.0.1 COREX_REMOTE_PORT={env['COREX_REMOTE_PORT']} dist/bin/runtime_server # independent PID={server.pid}\n")
    try:
        for _ in range(200):
            if server.poll() is not None:
                raise RuntimeError('server exited before readiness')
            try:
                with socket.create_connection(('127.0.0.1', int(env['COREX_REMOTE_PORT'])), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.05)
        else:
            raise RuntimeError('server readiness timeout')
        for command, label in clients:
            run(command, label, env)
        run(['python3', root / 'tests/integration/library_expansion_protocol.py', image], 'library-protocol', env)
        run(['python3', root / 'tests/integration/partial_context_protocol.py'], 'context-transfer-protocol', env)
    finally:
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill(); server.wait()
print('PARTIAL_SEMANTICS_VALIDATION=PASS', flush=True)
