#!/usr/bin/env python3
"""A staging transfer alone must prevent native context replacement."""
import os
import socket
import struct
from function_occupancy_protocol import call

with socket.create_connection(('127.0.0.1', int(os.environ['COREX_REMOTE_PORT'])), timeout=10) as sock:
    status, body = call(sock, 1, 1, struct.pack('!Q', 4))
    assert status == 0 and len(body) == 8
    allocation = struct.unpack('!Q', body)[0]
    assert call(sock, 2, 2, struct.pack('!QQQ', allocation, 0, 4) + b'\x35' * 4) == (0, b'')
    status, body = call(sock, 23, 3, struct.pack('!QQQQ', allocation, 0, 0, 4))
    assert status == 0 and len(body) == 8
    transfer = struct.unpack('!Q', body)[0]
    assert call(sock, 6, 4, struct.pack('!Q', allocation)) == (0, b'')
    # The allocation is gone; the native completion event and pinned staging
    # remain owned by TransferID until the client fetches the result.
    assert call(sock, 74, 5, struct.pack('!I', 1)) == (10, b'')
    assert call(sock, 25, 6, struct.pack('!Q', transfer)) == (0, b'\x35' * 4)
    assert call(sock, 74, 7, struct.pack('!I', 1)) == (0, b'')
    assert call(sock, 40, 8) == (0, struct.pack('!I', 1))
    assert call(sock, 7, 9) == (0, b'')
print('CONTEXT_FLAGS_TRANSFER_OWNER_PROTOCOL=PASS')
