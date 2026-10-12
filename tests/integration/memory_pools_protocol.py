#!/usr/bin/env python3
"""Pool/AllocationID lifetime and bounded DTO tests over independent CRX9."""
import os
import socket
import struct
from function_occupancy_protocol import call

endpoint = ('127.0.0.1', int(os.environ['COREX_REMOTE_PORT']))


def u64(*values):
    return struct.pack('!' + 'Q' * len(values), *values)


with socket.create_connection(endpoint, timeout=10) as sock:
    status, body = call(sock, 27, 1)
    assert status == 0
    hello = struct.unpack('!' + 'I' * (len(body) // 4), body)
    assert 8 in hello[11:], hello
    status, body = call(sock, 75, 2, struct.pack('!i', 0))
    assert status == 0 and len(body) == 8
    default_pool = struct.unpack('!Q', body)[0]
    assert call(sock, 76, 3, struct.pack('!i', 0)) == (0, body)
    status, body = call(sock, 78, 4, struct.pack('!IIi', 1, 1, 0))
    assert status == 0 and len(body) == 8
    pool = struct.unpack('!Q', body)[0]
    for req, (op, payload, want) in enumerate([
        (75, b'', 1), (75, struct.pack('!i', 1), 8),
        (78, struct.pack('!IIi', 0, 1, 0), 1),
        (78, struct.pack('!IIi', 1, 0, 0), 1),
        (79, u64(default_pool), 1), (80, u64(999, 0), 2),
        (81, struct.pack('!QI', pool, 9), 1),
        (82, struct.pack('!QIQ', pool, 1, 1), 10),
        (82, struct.pack('!QIQ', pool, 5, 0), 1),
        (83, struct.pack('!QIi', pool, 1, 1), 8),
        (84, struct.pack('!QIIiI', pool, 2, 1, 0, 3), 1),
        (84, struct.pack('!QIIiI', pool, 1, 1, 0, 1), 10),
        (85, u64(64, 999), 2), (85, u64(0, 0), 1),
        (86, u64(64, 0, 0), 1), (86, u64(64, 999, 0), 2),
        (87, u64(999, 0), 2), (74, struct.pack('!I', 1), 10),
    ], 10):
        actual = call(sock, op, req, payload)
        assert actual == (want, b''), (op, actual, want)
    # Every generated pool opcode rejects malformed lengths before side effects.
    for req, op in enumerate(range(75, 88), 40):
        assert call(sock, op, req, b'bad') == (1, b'')
    assert call(sock, 77, 60, struct.pack('!iQ', 0, pool)) == (0, b'')
    status, body = call(sock, 85, 61, u64(64, 0))
    assert status == 0 and len(body) == 8
    allocation = struct.unpack('!Q', body)[0]
    assert call(sock, 79, 62, u64(pool)) == (0, b'')
    assert call(sock, 76, 63, struct.pack('!i', 0)) == (0, u64(default_pool))
    assert call(sock, 79, 64, u64(pool)) == (2, b'')
    # Existing native allocation still lives after destroying its pool.
    assert call(sock, 29, 65, struct.pack('!QQIQ', allocation, 0, 0x5d, 64)) == (0, b'')
    assert call(sock, 87, 66, u64(allocation, 0)) == (0, b'')
    assert call(sock, 5, 67, u64(allocation, 0, 64)) == (2, b'')
    assert call(sock, 87, 68, u64(allocation, 0)) == (2, b'')
    assert call(sock, 4, 69) == (0, b'')
    # EOF cleanup restores native default state and retires outstanding pools.
    assert call(sock, 82, 70, struct.pack('!QIQ', default_pool, 4, 654321)) == (0, b'')
    status, body = call(sock, 78, 71, struct.pack('!IIi', 1, 1, 0))
    assert status == 0
    pool = struct.unpack('!Q', body)[0]
    assert call(sock, 77, 72, struct.pack('!iQ', 0, pool)) == (0, b'')
    assert call(sock, 85, 73, u64(64, 0))[0] == 0

with socket.create_connection(endpoint, timeout=10) as sock:
    status, body = call(sock, 75, 1, struct.pack('!i', 0))
    assert status == 0
    pool = struct.unpack('!Q', body)[0]
    assert call(sock, 81, 2, struct.pack('!QI', pool, 4)) == (0, u64(0))
    assert call(sock, 76, 3, struct.pack('!i', 0)) == (0, u64(pool))
    assert call(sock, 7, 4) == (0, b'')
print('MEMORY_POOLS_PROTOCOL=PASS capabilities bounded_DTO invalid_ID deferred_destroy async_free EOF_isolation')
