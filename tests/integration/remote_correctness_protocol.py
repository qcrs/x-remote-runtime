#!/usr/bin/env python3
"""Representative remote negotiation, session identity and protocol boundaries."""
import os
import socket
import struct
from pathlib import Path
from function_occupancy_protocol import call

endpoint = (os.environ['COREX_REMOTE_HOST'], int(os.environ['COREX_REMOTE_PORT']))
assert not endpoint[0].startswith('127.'), 'qualification requires a non-loopback endpoint'


def connect():
    return socket.create_connection(endpoint, timeout=10)


def graph_command(sock, action, graph=0, executable=0):
    return call(sock, 88, 50, struct.pack('!IQQQQIQI', action, graph, 0, executable, 0, 0, 0, 0))


def identity(response):
    status, data = response
    assert status == 0
    return struct.unpack('!Q', data[:8])[0]


with connect() as sock:
    print(f'CLIENT_PID={os.getpid()} NETNS={os.readlink("/proc/self/ns/net")} '
          f'LOCAL={sock.getsockname()} PEER={sock.getpeername()}', flush=True)
    status, body = call(sock, 27, 1)
    assert status == 0 and len(body) >= 44 and len(body) % 4 == 0
    hello = struct.unpack('!' + 'I' * (len(body) // 4), body)
    assert hello[:3] == (0x43525839, 3, 1), hello
    status, version = call(sock, 35, 1)
    assert status == 0 and struct.unpack('!I', version)[0] == hello[7], hello
    assert hello[10] == len(hello) - 11 and {1, 2, 3, 4, 5, 6, 8, 9} <= set(hello[11:]), hello
    print('HELLO=' + repr(hello), flush=True)
    for opcode, payload, want in [(27, b'bad', 1), (999, b'', 1), (88, b'bad', 1),
                                  (85, b'bad', 1), (82, struct.pack('!QIQ', 999, 1, 1), 2)]:
        assert call(sock, opcode, 2, payload) == (want, b'')
    # Measured unsupported context limit returns stable NotSupported twice.
    for request in [3, 4]:
        actual = call(sock, 59, request, struct.pack('!IQ', 0, 1024))
        assert actual == (3, b''), actual  # existing legacy wire status; public NotSupported
    assert call(sock, 27, 5)[0] == 0, 'rejection corrupted live connection'
    allocation = identity(call(sock, 1, 6, struct.pack('!Q', 64)))
    stream = identity(call(sock, 12, 7, struct.pack('!I', 0)))
    event = identity(call(sock, 16, 8, struct.pack('!I', 0)))
    pool = identity(call(sock, 78, 9, struct.pack('!IIi', 1, 1, 0)))
    graph = identity(graph_command(sock, 1))
    executable = identity(graph_command(sock, 8, graph=graph))
    image = (Path(__file__).resolve().parents[2] / 'build/session_lifecycle.cubin').read_bytes()
    name = b'remote-eof.cubin'
    module = identity(call(sock, 8, 10, struct.pack('!I', len(name)) + name + struct.pack('!Q', len(image)) + image))
    # EOF leaves all resource families live; no OP_CLOSE performs explicit teardown.

with connect() as sock:
    for opcode, payload in [(2, struct.pack('!QQQ', allocation, 0, 1) + b'x'),
                            (14, struct.pack('!Q', stream)), (19, struct.pack('!Q', event)),
                            (80, struct.pack('!QQ', pool, 0)), (10, struct.pack('!Q', module))]:
        assert call(sock, opcode, 1, payload) == (2, b''), (opcode, payload)
    assert graph_command(sock, 2, graph=graph) == (2, b'')
    assert graph_command(sock, 10, executable=executable) == (2, b'')
    assert call(sock, 4, 2) == (0, b'')
print('EOF_RESOURCE_ISOLATION=PASS allocation stream event graph exec pool module', flush=True)

for label, header, payload in [
    ('bad-version', struct.pack('!IIIII', 0x43525839, 99, 27, 1, 0), b''),
    ('over-limit', struct.pack('!IIIII', 0x43525839, 3, 27, 1, 64 * 1024 * 1024 + 1), b''),
    ('truncated-header', b'CRX', b''),
    ('truncated-payload', struct.pack('!IIIII', 0x43525839, 3, 1, 1, 8), b'x'),
]:
    with connect() as sock:
        sock.sendall(header + payload)
        sock.shutdown(socket.SHUT_WR)
        try:
            assert sock.recv(1) == b'', label
        except ConnectionResetError:
            pass
    with connect() as sock:
        assert call(sock, 27, 1)[0] == 0, label + ' corrupted next session'
    print(label + '=PASS', flush=True)
print('REMOTE_NEGOTIATION_TRUST=PASS', flush=True)
