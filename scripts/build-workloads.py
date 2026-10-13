#!/usr/bin/env python3
"""Compile ordinary CUDA source; link only the public remote Runtime SDK."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True, help='build/provenance logs only')
p.add_argument('names', nargs='*')
args = p.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
work = root / 'build/workloads'
work.mkdir(parents=True, exist_ok=True)
corex = Path(os.environ.get('COREX', '/usr/local/corex-4.4.0'))
major = subprocess.check_output(['g++', '-dumpfullversion'], text=True).split('.')[0]
includes = []
for path in [f'/usr/include/c++/{major}', f'/usr/include/aarch64-linux-gnu/c++/{major}', f'/usr/include/c++/{major}/backward']:
    if Path(path).exists():
        includes += ['-isystem', path]
commands = []
names = args.names or sorted(f.stem for f in (root / 'tests/workloads').glob('*.cu'))
for name in names:
    source = root / 'tests/workloads' / (name + '.cu')
    obj = work / (name + '.o')
    binary = work / name
    steps = [([str(corex / 'bin/clang++'), '-x', 'ivcore', f'--cuda-path={corex}', f'-I{corex}/include', *includes, '-c', str(source), '-o', str(obj)], 'compile'),
             (['g++', str(obj), f'-L{root}/dist/lib', '-lcorex_remote_cudart', f'-Wl,-rpath,{root}/dist/lib', '-pthread', '-o', str(binary)], 'link'),
             (['readelf', '-dW', str(binary)], 'dependencies'),
             (['readelf', '-hW', str(binary)], 'architecture'),
             (['ldd', str(binary)], 'resolved-libraries'),
             (['nm', '-u', str(obj)], 'compiler-abi')]
    for command, label in steps:
        commands.append(shlex.join(command))
        (out / 'COMMANDS.txt').write_text('\n'.join(commands) + '\n')
        with (out / f'{name}-{label}.stdout.log').open('w') as stdout, (out / f'{name}-{label}.stderr.log').open('w') as stderr:
            subprocess.run(command, cwd=root, check=True, stdout=stdout, stderr=stderr)
    deps = (out / f'{name}-dependencies.stdout.log').read_text()
    abi = (out / f'{name}-compiler-abi.stdout.log').read_text()
    resolved = (out / f'{name}-resolved-libraries.stdout.log').read_text()
    assert 'libcorex_remote_cudart.so.1' in deps
    assert 'libcudart' not in deps and 'libcuda.so' not in deps
    assert str(root / 'dist/lib/libcorex_remote_cudart.so.1') in resolved
    assert 'libcudart' not in resolved and 'libcuda.so' not in resolved
    for symbol in ['__cudaRegisterFatBinary', '__cudaRegisterFunction', 'cudaLaunchKernel']:
        assert symbol in abi, (name, symbol)
    print(f'{name}_BUILD=PASS', flush=True)
