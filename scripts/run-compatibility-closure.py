#!/usr/bin/env python3
"""Grouped compatibility closure probes; logs only in the evidence root."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import ctypes
import ctypes.util
import glob
import csv
import re
import socket
import time

root = Path(__file__).resolve().parents[1]
os.environ['PYTHONDONTWRITEBYTECODE'] = '1'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, default=root / 'evidence/m8/compatibility-closure-20261013')
parser.add_argument('--phase', choices=['required', 'core', 'remote', 'legacy'], default='required')
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
with (out / 'COMMANDS.txt').open('a') as f:
    f.write(f'python3 scripts/run-compatibility-closure.py --phase {args.phase} --out {shlex.quote(str(out))}\n')
work = root / 'build/compatibility-closure'
work.mkdir(parents=True, exist_ok=True)
corex = Path(os.environ.get('COREX', '/usr/local/corex-4.4.0'))


def run(command, label, allow_failure=False, env=None):
    with (out / 'COMMANDS.txt').open('a') as f:
        f.write(shlex.join(map(str, command)) + '\n')
    with (out / (label + '.stdout.log')).open('w') as stdout, (out / (label + '.stderr.log')).open('w') as stderr:
        result = subprocess.run(list(map(str, command)), cwd=root, env=env, stdout=stdout, stderr=stderr, timeout=60)
    (out / (label + '.exit-code.txt')).write_text(str(result.returncode) + '\n')
    if result.returncode and not allow_failure:
        raise RuntimeError(f'{label} failed: {result.returncode}')
    print(f'{label}: exit={result.returncode}', flush=True)
    return result.returncode


common = ['gcc', '-O2', '-Wall', '-Wextra', '-Werror', '-std=gnu11', f'-I{corex}/include']
probes = [
    ('corex_closure_peer_ipc_probe.c', 'cuda', 'peer-ipc'),
    ('corex_compat_families_probe.c', 'cuda', 'array-managed-driver'),
    ('corex_compat_runtime_probe.c', 'cudart', 'array-managed-runtime'),
    ('corex_closure_runtime_probe.c', 'cudart', 'closure-runtime'),
] if args.phase == 'required' else []
for source, library, label in probes:
    binary = work / label
    run(common + [root / 'tests/probes' / source, f'-L{corex}/lib64',
                  f'-Wl,-rpath,{corex}/lib64', '-l' + library, '-ldl', '-o', binary], label + '-compile')
    run([binary], label)

major = subprocess.check_output(['g++', '-dumpversion'], text=True).strip().split('.')[0]
includes = []
for p in [f'/usr/include/c++/{major}', f'/usr/include/aarch64-linux-gnu/c++/{major}',
          f'/usr/include/c++/{major}/backward']:
    if Path(p).exists():
        includes += ['-isystem', p]
obj = work / 'legacy.o'
if args.phase == 'remote':
    binary = work / 'closure-remote'
    run(common + [root / 'tests/integration/compatibility_closure.c', f'-L{root}/dist/lib',
                  f'-Wl,-rpath,{root}/dist/lib', '-lcorex_remote_cudart', '-o', binary], 'closure-remote-compile')
    obj = work / 'function-client.o'
    run([corex / 'bin/clang++', '-x', 'ivcore', f'--cuda-path={corex}', f'-I{corex}/include'] + includes +
        ['-c', root / 'tests/integration/compatibility_closure_function.cu', '-o', obj], 'function-client-compile')
    function_binary = work / 'function-client'
    run(['g++', obj, f'-L{root}/dist/lib', f'-Wl,-rpath,{root}/dist/lib', '-lcorex_remote_cudart',
         '-o', function_binary], 'function-client-link')
    other_clients = []
    for source in ['compat_expansion', 'legacy_control']:
        app = work / source
        run(common + [root / 'tests/integration' / (source + '.c'), f'-L{root}/dist/lib',
                      f'-Wl,-rpath,{root}/dist/lib', '-lcorex_remote_cudart', '-o', app], source + '-compile')
        other_clients.append(app)
    expected = re.search(r'FUNCTION_ATTRIBUTE id=8 result=0 value=(\d+)',
                         (out / 'function-probe.stdout.log').read_text()).group(1)
    env = os.environ.copy()
    env['COREX_REMOTE_BIND'] = env['COREX_REMOTE_HOST'] = '127.0.0.1'
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1', 0))
        env['COREX_REMOTE_PORT'] = str(reserve.getsockname()[1])
    with (out / 'closure-server.stdout.log').open('w') as stdout, (out / 'closure-server.stderr.log').open('w') as stderr:
        server = subprocess.Popen([root / 'dist/bin/runtime_server'], cwd=root, env=env, stdout=stdout, stderr=stderr)
        with (out / 'COMMANDS.txt').open('a') as f:
            f.write(f"COREX_REMOTE_PORT={env['COREX_REMOTE_PORT']} dist/bin/runtime_server # independent PID={server.pid}\n")
        try:
            for _ in range(200):
                if server.poll() is not None:
                    raise RuntimeError('server exited')
                try:
                    with socket.create_connection(('127.0.0.1', int(env['COREX_REMOTE_PORT'])), timeout=0.1):
                        break
                except OSError:
                    time.sleep(0.05)
            else:
                raise RuntimeError('server readiness timeout')
            run([binary, out / 'error-enum-native.tsv'], 'closure-remote', env=env)
            run([function_binary, expected], 'function-remote', env=env)
            for app in other_clients:
                run([app], app.name + '-remote', env=env)
            run(['python3', root / 'tests/integration/compat_expansion_protocol.py'], 'host-stream-protocol', env=env)
            run(['python3', root / 'tests/integration/compatibility_closure_protocol.py',
                 work / 'functions.cubin', expected], 'function-protocol', env=env)
        finally:
            server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill(); server.wait()
    print('CLOSURE_REMOTE_FAMILIES=PASS', flush=True)
    raise SystemExit(0)

if args.phase == 'core':
    image = work / 'functions.cubin'
    run([corex / 'bin/clang++', '-x', 'ivcore', f'--cuda-path={corex}', '--cuda-device-only',
         f'-I{corex}/include'] + includes + [root / 'tests/integration/library_kernels.cu', '-o', image],
        'function-image-compile')
    binary = work / 'functions'
    run(common + [root / 'tests/probes/corex_closure_function_probe.c', f'-L{corex}/lib64',
                  f'-Wl,-rpath,{corex}/lib64', '-lcuda', '-o', binary], 'function-probe-compile')
    run([binary, image], 'function-probe')
    raise SystemExit(0)

lines = []
for p in ['/dev/dri', '/usr/share/vulkan/icd.d']:
    lines.append(p + ': ' + str(glob.glob(p + '/*')))
for name in ['nvscisync', 'nvscibuf', 'vulkan', 'EGL', 'GL']:
    lines.append(name + ': ' + str(ctypes.util.find_library(name)))
for key in ['DISPLAY', 'WAYLAND_DISPLAY']:
    lines.append(key + '=' + str(os.environ.get(key)))
for library, names in [('cuda', ['cuImportExternalMemory', 'cuImportExternalSemaphore',
                                'cuGraphicsGLRegisterBuffer', 'cuDeviceGetNvSciSyncAttributes']),
                       ('cudart', ['cudaImportExternalMemory', 'cudaImportExternalSemaphore',
                                   'cudaGraphicsGLRegisterBuffer', 'cudaDeviceGetNvSciSyncAttributes'])]:
    dso = ctypes.CDLL(str(corex / 'lib64' / ('lib' + library + '.so')))
    for name in names:
        lines.append(library + ' ' + name + ' SYMBOL=' + ('PRESENT' if hasattr(dso, name) else 'ABSENT'))
(out / 'interop-environment.txt').write_text('\n'.join(lines) + '\n')
enum_values = re.findall(r'^\s*(cudaSuccess|cudaError\w+)\s*=\s*(\d+)',
                        (corex / 'include/driver_types.h').read_text(), re.M)
dso = ctypes.CDLL(str(corex / 'lib64/libcudart.so'))
dso.cudaGetErrorName.restype = dso.cudaGetErrorString.restype = ctypes.c_char_p
with (out / 'error-enum-native.tsv').open('w') as f:
    writer = csv.writer(f, delimiter='\t', lineterminator='\n')
    writer.writerow(['value', 'header_name', 'native_name', 'native_description'])
    for name, value in enum_values:
        writer.writerow([value, name, dso.cudaGetErrorName(int(value)).decode(), dso.cudaGetErrorString(int(value)).decode()])
run([corex / 'bin/clang++', '-x', 'ivcore', f'--cuda-path={corex}', f'-I{corex}/include', '-DPROBE_LEGACY_FETCH'] +
    includes + ['-c', root / 'tests/probes/corex_closure_legacy_probe.cu', '-o', obj],
    'legacy-fetch-compile', allow_failure=True)
if not run([corex / 'bin/clang++', '-x', 'ivcore', f'--cuda-path={corex}', f'-I{corex}/include'] +
           includes + ['-c', root / 'tests/probes/corex_closure_legacy_probe.cu', '-o', obj],
           'legacy-compile', allow_failure=True):
    run(['nm', '-u', obj], 'legacy-registration-symbols')
    binary = work / 'legacy'
    if not run(['g++', obj, f'-L{corex}/lib64', f'-Wl,-rpath,{corex}/lib64', '-lcudart', '-o', binary],
               'legacy-link', allow_failure=True):
        run([binary], 'legacy')
