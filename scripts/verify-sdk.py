#!/usr/bin/env python3
"""Validate an installed/extracted SDK using its packaged contracts."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('prefix', type=Path)
args = p.parse_args()
prefix = args.prefix.resolve()
lib = prefix / 'lib/libcorex_remote_cudart.so.1.1.0'
share = prefix / 'share/corex-remote-cudart'

def output(*command):
    return subprocess.check_output(command, text=True)

assert (prefix / 'lib/libcorex_remote_cudart.so').readlink() == Path('libcorex_remote_cudart.so.1')
assert (prefix / 'lib/libcorex_remote_cudart.so.1').readlink() == Path(lib.name)
dynamic = output('readelf', '-dW', str(lib))
assert '(SONAME)' in dynamic and '[libcorex_remote_cudart.so.1]' in dynamic
assert not re.search(r'libcudart|libcuda[.]so|\((?:RUNPATH|RPATH)\)', dynamic)
resolved = output('ldd', str(lib))
assert 'not found' not in resolved and not re.search(r'libcudart|libcuda[.]so', resolved)
print(dynamic, end='')
print(resolved, end='')
symbols = output('nm', '-D', '--defined-only', str(lib))
actual = {}
for line in symbols.splitlines():
    cols = line.split()
    if len(cols) == 3 and cols[1].upper() in {'T', 'W'}:
        name, version = cols[2].split('@@')
        actual[name] = version
expected = {line.strip() for line in (share / 'EXPECTED-EXPORTED-FUNCTIONS.txt').read_text().splitlines()
            if line.strip() and not line.startswith('#')}
assert set(actual) == expected
mapped = {}
for version, body in re.findall(r'(COREX_REMOTE_[A-Za-z0-9_.]+)\s*\{\s*global:(.*?)(?:local:.*?)?\}',
                               (share / 'corex_remote_cudart.map').read_text(), re.S):
    mapped.update({name.strip(): version for name in body.split(';') if name.strip()})
assert actual == mapped
surface = {}
version = None
for line in (share / 'ABI-SURFACE-v1.1.txt').read_text().splitlines():
    if line.startswith('# Internal'):
        break
    if line.startswith('['):
        version = line.strip('[]')
    elif line and not line.startswith('#'):
        surface[line] = version
assert actual == surface
env = dict(os.environ, PKG_CONFIG_PATH=str(prefix / 'lib/pkgconfig'))
metadata = (prefix / 'lib/pkgconfig/corex-remote-cudart.pc').read_text()
assert 'prefix=${pcfiledir}/../..\n' in metadata
assert 'Libs: -L${libdir} -lcorex_remote_cudart\n' in metadata
assert 'Cflags: -I${includedir}\n' in metadata
if shutil.which('pkg-config'):
    for option in ['--cflags', '--libs']:
        flags = subprocess.check_output(['pkg-config', option, 'corex-remote-cudart'], env=env, text=True)
        print(option + '=' + flags.strip())
        paths = re.findall(r'-[IL](\S+)', flags)
        assert paths and all(Path(path).resolve().is_relative_to(prefix) for path in paths)
    print('SDK_PKGCONFIG_MODE=TOOL')
else:
    print('SDK_PKGCONFIG_MODE=METADATA_ONLY (consumer uses explicit-prefix fallback)')
corex = Path(os.environ.get('COREX', '/usr/local/corex-4.4.0'))
subprocess.run(['g++', '-x', 'c++', '-fsyntax-only', f'-I{prefix}/include', f'-I{corex}/include', '-'],
               input='#include <corex_remote_cudart_ext.h>\n', text=True, check=True)
assert os.access(prefix / 'bin/corex-remote-runtime-server', os.X_OK)
assert {f.name for f in (prefix / 'include').iterdir()} == {'corex_remote_cudart_ext.h'}
print('SDK_SONAME_SYMLINKS=PASS')
print('SDK_EXPORT_VERSIONS=PASS')
print('SDK_PUBLIC_HEADER=PASS (standard CUDA headers supplied by CoreX SDK)')
print('SDK_CLIENT_NATIVE_DEPENDENCIES=ABSENT')
print('SDK_VALIDATION=PASS')
