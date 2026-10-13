#!/usr/bin/env python3
"""Same-host correctness over isolated client netns/veth and real CoreX.

Run after scripts/build.sh. Root uses the initial server netns; an ordinary
user uses unshare -Urn for a private server netns (GPU devices stay accessible).
Only namespaces, processes and veth links owned by this run are removed.
"""
import argparse
import os
from pathlib import Path
import platform
import shlex
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--inside-rootless', action='store_true', help=argparse.SUPPRESS)
parser.add_argument('--topology-only', action='store_true')
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
os.environ.setdefault('COREX_RC_ORIGINAL_UID', str(os.getuid()))
os.environ.setdefault('COREX_RC_HOST_NETNS', os.readlink('/proc/self/ns/net'))
if os.geteuid() != 0:
    os.execvp('unshare', ['unshare', '-Urn', sys.executable, str(Path(__file__).resolve()),
                         '--inside-rootless', *sys.argv[1:]])

work = ROOT / 'build/remote-correctness'
work.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env.update(COREX_REMOTE_HOST='198.18.0.1', COREX_REMOTE_BIND='198.18.0.1',
           COREX_REMOTE_PORT='50991', COREX_GRAPH_EXPANSION='1', PYTHONDONTWRITEBYTECODE='1')
server = None
keeper = None
logs = []
prefix = []
commands = []


def run(command, label, client=False, timeout=90, expected=0):
    command = list(map(str, command))
    if client:
        command = prefix + command
    commands.append(shlex.join(command))
    (out / 'COMMANDS.txt').write_text('\n'.join(commands) + '\n')
    with (out / (label + '.stdout.log')).open('w') as stdout, (out / (label + '.stderr.log')).open('w') as stderr:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=stdout, stderr=stderr, timeout=timeout)
    if result.returncode != expected:
        raise RuntimeError(f'{label} failed ({result.returncode}); see {out}')
    print(label + '=PASS', flush=True)


def start_server(label):
    global server
    stdout = (out / (label + '.stdout.log')).open('w')
    stderr = (out / (label + '.stderr.log')).open('w')
    logs.extend([stdout, stderr])
    server = subprocess.Popen([ROOT / 'dist/bin/runtime_server'], cwd=ROOT, env=env, stdout=stdout, stderr=stderr)
    commands.append(f'COREX_REMOTE_BIND=198.18.0.1 COREX_REMOTE_PORT=50991 dist/bin/runtime_server # PID={server.pid}')
    (out / 'COMMANDS.txt').write_text('\n'.join(commands) + '\n')
    for _ in range(200):
        if server.poll() is not None:
            raise RuntimeError('CoreX server exited before readiness')
        if 'SERVER_READY' in (out / (label + '.stdout.log')).read_text():
            return
        time.sleep(.05)
    raise RuntimeError('CoreX server readiness timeout')


def stop_server():
    global server
    if server and server.poll() is None:
        server.terminate()
        server.wait(timeout=10)
    server = None


try:
    # The keeper shares the server's user namespace but owns a separate netns.
    keeper = subprocess.Popen(['unshare', '-n', 'sleep', '600'])
    server_ns = os.readlink('/proc/self/ns/net')
    for _ in range(100):
        if keeper.poll() is not None:
            raise RuntimeError('client namespace keeper exited')
        client_ns = os.readlink(f'/proc/{keeper.pid}/ns/net')
        if client_ns != server_ns:
            break
        time.sleep(.01)
    else:
        raise RuntimeError('client network namespace was not isolated')
    prefix = ['nsenter', f'--net=/proc/{keeper.pid}/ns/net', '--']
    for command in [
        ['ip', 'link', 'add', 'rc-server', 'type', 'veth', 'peer', 'name', 'rc-client'],
        ['ip', 'link', 'set', 'rc-client', 'netns', str(keeper.pid)],
        ['ip', 'address', 'add', '198.18.0.1/30', 'dev', 'rc-server'],
        ['ip', 'link', 'set', 'rc-server', 'up'],
    ]:
        subprocess.run(command, check=True)
        commands.append(shlex.join(command))
    if args.inside_rootless:
        subprocess.run(['ip', 'link', 'set', 'lo', 'up'], check=True)
    for command in [['ip', 'address', 'add', '198.18.0.2/30', 'dev', 'rc-client'],
                    ['ip', 'link', 'set', 'rc-client', 'up'], ['ip', 'link', 'set', 'lo', 'up']]:
        subprocess.run(prefix + command, check=True)
        commands.append(shlex.join(prefix + command))
    (out / 'ENVIRONMENT.txt').write_text(
        f'UTC={time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}\nHOST={platform.node()}\n'
        f'CLIENT_ARCH={platform.machine()}\nSERVER_ARCH={platform.machine()}\n'
        'COREX=4.4.0\nGPU=MR-V100 (confirmed by server log)\n'
        f'VALIDATION_HEAD={subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()}\n'
        f'SERVER_NETNS={server_ns}\nCLIENT_NETNS={client_ns}\nCLIENT_KEEPER_PID={keeper.pid}\n'
        f'INITIAL_HOST_NETNS={env["COREX_RC_HOST_NETNS"]}\nORIGINAL_UID={env["COREX_RC_ORIGINAL_UID"]}\n'
        f'SERVER_NAMESPACE_MODE={"rootless user-owned network namespace" if args.inside_rootless else "initial host namespace"}\n'
        'TOPOLOGY=same-host emulation; independent client/server processes; isolated client netns '
        '198.18.0.2 -> veth/TCP 198.18.0.1:50991 -> server -> real CoreX Driver -> MR-V100\n'
        'No physical two-host, performance or cross-architecture claim. No device/mount namespace.\n'
        'Rootless mode keeps devices/filesystem on host and leaves initial host network unchanged.\n')
    run(['ip', '-details', 'address', 'show', 'rc-server'], 'topology-server')
    run(['ip', '-details', 'address', 'show', 'rc-client'], 'topology-client', client=True)
    run(['ip', 'route', 'get', '198.18.0.1'], 'topology-route', client=True)
    run(['env', 'COREX_REMOTE_BIND=invalid-address', ROOT / 'dist/bin/runtime_server'],
        'bind-invalid', expected=1)
    assert 'COREX_REMOTE_BIND must be an IPv4 address' in (out / 'bind-invalid.stderr.log').read_text()
    start_server('server')
    with (out / 'ENVIRONMENT.txt').open('a') as record:
        record.write(f'SERVER_PID={server.pid}\n')
    run(['python3', ROOT / 'tests/integration/remote_correctness_protocol.py'], 'negotiation-trust', client=True)
    if args.topology_only:
        print('REMOTE_TOPOLOGY=PASS', flush=True)
    else:
        corex = Path(env.get('COREX', '/usr/local/corex-4.4.0'))
        common = ['gcc', '-O2', '-Wall', '-Wextra', '-Werror', '-std=gnu11', '-Wno-deprecated-declarations',
                  f'-I{corex}/include']
        image = ROOT / 'build/session_lifecycle.cubin'
        # Existing public-header clients compose each capability family.
        clients = [('compat_expansion', []), ('memory_layout', []), ('memory_3d', []),
                   ('execution_fill', [image]), ('memory_pools', [image]),
                   ('graphs', [image]), ('graph_expansion', [image]), ('graph_memory', [image]),
                   ('graph_host', []), ('graph_controls', [out / 'client-graph.dot'])]
        for name, parameters in clients:
            binary = work / name
            run(common + [ROOT / f'tests/integration/{name}.c', f'-L{ROOT}/dist/lib',
                          '-lcorex_remote_cudart', f'-Wl,-rpath,{ROOT}/dist/lib', '-pthread', '-ldl', '-o', binary], name + '-compile')
            run([binary, *parameters], name, client=True)
        # Compiler-generated kernel and global/constant registration.
        version = subprocess.check_output(['g++', '-dumpfullversion'], text=True).split('.')[0]
        includes = []
        for path in [f'/usr/include/c++/{version}', f'/usr/include/aarch64-linux-gnu/c++/{version}',
                     f'/usr/include/c++/{version}/backward']:
            if Path(path).exists():
                includes += ['-isystem', path]
        run(['gcc', '-c', ROOT / 'tests/integration/symbol_registration_capture.c', '-o', work / 'capture.o'], 'symbols-capture-compile')
        run([corex / 'bin/clang++', '-x', 'ivcore', f'--cuda-path={corex}', f'-I{corex}/include', *includes,
             '-c', ROOT / 'tests/integration/symbol_expansion.cu', '-o', work / 'symbols.o'], 'symbols-compile')
        run(['g++', work / 'symbols.o', work / 'capture.o', f'-L{ROOT}/dist/lib', '-lcorex_remote_cudart',
             f'-Wl,-rpath,{ROOT}/dist/lib', '-ldl', '-o', work / 'symbols'], 'symbols-link')
        run([work / 'symbols'], 'symbols', client=True)
        run([work / 'symbols', 'leave-module-live'], 'symbols-live-exit', client=True)
        run([corex / 'bin/clang++', '-x', 'ivcore', f'--cuda-path={corex}', '--cuda-device-only',
             f'-I{corex}/include', *includes, ROOT / 'tests/probes/corex_symbol_kernel.cu',
             '-o', work / 'symbols.cubin'], 'symbols-image-compile')
        run(['python3', ROOT / 'tests/integration/symbol_expansion_protocol.py', work / 'symbols.cubin'],
            'symbols-wire', client=True)
        run([ROOT / 'dist/bin/device_identity_app'], 'device-identity', client=True)
        run([ROOT / 'dist/bin/device_config_app'], 'device-controls', client=True)
        for name in ['compat_expansion', 'memory_3d', 'execution_fill', 'memory_pools', 'graphs']:
            run(['python3', ROOT / f'tests/integration/{name}_protocol.py'], name + '-wire', client=True)
        run(['python3', ROOT / 'tests/integration/remote_correctness_failure.py', '--exit-live'], 'abrupt-client-exit', client=True)
        # A fresh session after abrupt exit must still negotiate and execute.
        run(['python3', ROOT / 'tests/integration/remote_correctness_protocol.py'], 'after-abrupt-exit', client=True)
        # Real server kill/restart is controlled from the server namespace.
        left, right = socket.socketpair()
        with (out / 'failure.stdout.log').open('w') as stdout, (out / 'failure.stderr.log').open('w') as stderr:
            failure_cmd = prefix + [sys.executable, str(ROOT / 'tests/integration/remote_correctness_failure.py'), str(right.fileno())]
            commands.append(shlex.join(failure_cmd))
            (out / 'COMMANDS.txt').write_text('\n'.join(commands) + '\n')
            failure = subprocess.Popen(failure_cmd, cwd=ROOT, env=env, pass_fds=[right.fileno()], stdout=stdout, stderr=stderr)
            right.close()
            left.settimeout(30)
            try:
                assert left.recv(32) == b'kill', 'missing live-resource kill rendezvous'
                stop_server()
                start_server('server-reconnected')
                left.sendall(b'restarted')
                failure.wait(timeout=60)
                if failure.returncode:
                    raise RuntimeError('failure client failed; see failure logs')
            finally:
                left.close()
                if failure.poll() is None:
                    failure.kill()
                    failure.wait()
        print('failure=PASS', flush=True)
        run(['python3', ROOT / 'tests/integration/remote_correctness_protocol.py'], 'post-failure-trust', client=True)
        print('REMOTE_CORRECTNESS=PASS', flush=True)
finally:
    stop_server()
    for log in logs:
        log.close()
    # Remove the named link only if it was created by this run.
    if any(command.startswith('ip link add rc-server ') for command in commands):
        subprocess.run(['ip', 'link', 'delete', 'rc-server'], check=True)
    if keeper and keeper.poll() is None:
        keeper.terminate()
        keeper.wait(timeout=5)
    (out / 'cleanup.txt').write_text('Owned server/client processes stopped; owned veth removed; client netns released.\n')
