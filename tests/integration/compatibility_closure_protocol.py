#!/usr/bin/env python3
"""Verify old and extended function DTOs against the same native KernelID."""
import os
import socket
import struct
import sys
from function_occupancy_protocol import call

image = open(sys.argv[1], 'rb').read()
name = b'closure.cubin'
with socket.create_connection((os.environ['COREX_REMOTE_HOST'], int(os.environ['COREX_REMOTE_PORT'])), timeout=10) as sock:
    status, body = call(sock, 8, 1, struct.pack('!I', len(name)) + name + struct.pack('!Q', len(image)) + image)
    assert status == 0 and len(body) == 8
    module = struct.unpack('!Q', body)[0]
    name = b'm1s2_lifecycle_kernel'
    status, body = call(sock, 9, 2, struct.pack('!QI', module, len(name)) + name)
    assert status == 0 and len(body) == 8
    old_status, old = call(sock, 37, 3, body)
    full_status, full = call(sock, 92, 4, body)
    assert old_status == full_status == 0
    assert len(old) == 28 and len(full) == 36 and old == full[:28]
    values = struct.unpack('!9i', full)
    assert values[7] == 0 and values[8] == int(sys.argv[2])
    assert call(sock, 10, 5, struct.pack('!Q', module)) == (0, b'')
    assert call(sock, 92, 6, body) == (2, b'')
    call(sock, 7, 7)
print('FUNCTION_INFO_OLD_WIRE_EXTENDED_WIRE_AND_STALE_OWNER=PASS')
