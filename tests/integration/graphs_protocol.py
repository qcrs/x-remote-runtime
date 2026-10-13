#!/usr/bin/env python3
"""Graph/capture ID ownership, bounded commands and EOF cleanup over CRX9."""
import os
import socket
import struct
from function_occupancy_protocol import call

endpoint = (os.environ.get("COREX_REMOTE_HOST", "127.0.0.1"), int(os.environ["COREX_REMOTE_PORT"]))
request = 0


def command(sock, action, graph=0, node=0, exec_id=0, stream=0, kind=0, value=0, data=b""):
    global request
    request += 1
    payload = struct.pack("!IQQQQIQI", action, graph, node, exec_id, stream, kind, value, len(data)) + data
    return call(sock, 88, request, payload)


def object_id(result):
    status, data = result
    assert status == 0 and len(data) >= 16, result
    return struct.unpack("!Q", data[:8])[0]


with socket.create_connection(endpoint, timeout=10) as sock:
    status, hello = call(sock, 27, 1)
    assert status == 0 and 9 in struct.unpack("!" + "I" * (len(hello)//4), hello)[11:]
    for payload in [b"", b"bad", b"\0"*51, b"\0"*53]:
        assert call(sock, 88, 2, payload) == (1, b"")
    graph = object_id(command(sock, 1))
    assert command(sock, 1, value=1) == (1, b"")
    assert command(sock, 999) == (1, b"")
    assert command(sock, 3, graph=graph, kind=5, data=struct.pack("!I", 65)) == (1, b"")
    first = object_id(command(sock, 3, graph=graph, kind=5, data=struct.pack("!I", 0)))
    second = object_id(command(sock, 3, graph=graph, kind=5, data=struct.pack("!IQ", 1, first)))
    assert command(sock, 3, graph=graph, kind=5, data=struct.pack("!IQQ", 2, first, first)) == (1, b"")
    assert command(sock, 3, graph=graph, kind=2, data=struct.pack("!I", 0)+b"bad") == (1, b"")
    foreign = object_id(command(sock, 1))
    assert command(sock, 3, graph=foreign, kind=5, data=struct.pack("!IQ", 1, first)) == (1, b"")
    executable = object_id(command(sock, 8, graph=graph))
    assert command(sock, 10, exec_id=999) == (2, b"")
    assert command(sock, 10, exec_id=executable, stream=999) == (2, b"")
    assert command(sock, 19, node=first, exec_id=executable, value=1) == (1, b"")
    assert command(sock, 2, graph=graph)[0] == 0
    assert command(sock, 16, node=first) == (2, b"")
    assert command(sock, 10, exec_id=executable)[0] == 0
    assert call(sock, 4, 20) == (0, b"")
    # Leave an executable and graph to exercise the EOF cleanup path.

with socket.create_connection(endpoint, timeout=10) as sock:
    assert command(sock, 10, exec_id=executable) == (2, b"")
    status, data = call(sock, 12, 1, struct.pack("!I", 1))
    assert status == 0
    stream = struct.unpack("!Q", data)[0]
    # Actions are pinned in corex_graph_protocol.h: clone/find/update precede capture.
    capture = object_id(command(sock, 24, stream=stream, value=1))
    assert call(sock, 1, 2, struct.pack("!Q", 64)) == (11, b"")
    assert command(sock, 1) == (10, b"")
    assert command(sock, 25, graph=capture, stream=stream, kind=2, data=b"bad") == (1, b"")
    # EOF during active native capture must restore a usable next session.

with socket.create_connection(endpoint, timeout=10) as sock:
    graph = object_id(command(sock, 1))
    first = object_id(command(sock, 3, graph=graph, kind=5, data=struct.pack("!I", 0)))
    executable = object_id(command(sock, 8, graph=graph))
    assert command(sock, 10, exec_id=executable)[0] == 0
    assert call(sock, 4, 1) == (0, b"")
    assert call(sock, 7, 2) == (0, b"")
print("GRAPHS_PROTOCOL=PASS bounded_commands stale_IDs graph_exec_ownership capture_EOF session_isolation")
if os.environ.get("COREX_GRAPH_EXPANSION"):
    with socket.create_connection(endpoint, timeout=10) as sock:
        for opcode,size in [(89,8),(90,16),(91,4)]:
            assert call(sock,opcode,10,b"bad") == (1,b"")
            assert call(sock,opcode,11,b"\0"*(size+1)) == (1,b"")
        assert call(sock,89,12,struct.pack("!iI",1,0)) == (8,b"")
        assert call(sock,89,13,struct.pack("!iI",0,4)) == (1,b"")
        assert call(sock,90,14,struct.pack("!iIQ",0,1,1)) == (1,b"")
        assert call(sock,90,15,struct.pack("!iIQ",0,0,0)) == (1,b"")
        assert call(sock,89,16,struct.pack("!iI",0,0))[0] == 0
        graph=object_id(command(sock,1))
        first=object_id(command(sock,3,graph=graph,kind=5,data=struct.pack("!I",0)))
        foreign=object_id(command(sock,1))
        foreign_node=object_id(command(sock,3,graph=foreign,kind=5,data=struct.pack("!I",0)))
        child_parent=object_id(command(sock,1))
        assert command(sock,30,graph=child_parent,value=foreign,data=b"bad") == (1,b"")
        assert command(sock,30,graph=child_parent,value=child_parent) == (1,b"")
        child_result=command(sock,30,graph=child_parent,value=foreign)
        child_node=object_id(child_result)
        borrowed=struct.unpack("!Q",child_result[1][16:24])[0]
        assert command(sock,2,graph=borrowed) == (10,b"")
        assert command(sock,30,graph=graph,value=child_parent) == (10,b"")
        child_exec=object_id(command(sock,8,graph=child_parent))
        assert command(sock,31,node=first,exec_id=child_exec,value=foreign) == (1,b"")
        assert command(sock,31,node=child_node,exec_id=child_exec,value=foreign,data=b"bad") == (1,b"")
        assert command(sock,31,node=child_node,exec_id=child_exec,value=foreign)[0] == 0
        assert command(sock,10,exec_id=child_exec)[0] == 0
        status,data=call(sock,12,17,struct.pack("!I",1));assert status==0
        stream=struct.unpack("!Q",data)[0]
        assert command(sock,28,graph=graph,stream=stream,value=1,data=b"bad") == (1,b"")
        assert command(sock,28,graph=graph,stream=stream,value=1,data=struct.pack("!Q",foreign_node)) == (1,b"")
        assert object_id(command(sock,28,graph=graph,stream=stream,value=1,data=struct.pack("!Q",first)))==graph
        assert command(sock,29,graph=graph,stream=stream,value=2) == (1,b"")
        assert command(sock,29,graph=graph,stream=stream,value=1,data=b"bad") == (1,b"")
        assert command(sock,29,graph=graph,stream=stream,value=1,data=struct.pack("!Q",foreign_node)) == (1,b"")
        assert command(sock,29,graph=graph,stream=stream,value=1,data=struct.pack("!Q",first))[0]==0
        # EOF must finish supplied-graph capture before releasing native graph ownership.
    with socket.create_connection(endpoint, timeout=10) as sock:
        graph=object_id(command(sock,1))
        executable=object_id(command(sock,8,graph=graph))
        assert command(sock,10,exec_id=executable)[0]==0
        assert call(sock,4,18)==(0,b"")
    with socket.create_connection(endpoint,timeout=10) as sock:
        graph=object_id(command(sock,1))
        assert command(sock,32,graph=graph,value=32,data=b"bad")== (1,b"")
        assert command(sock,32,graph=graph,value=0)== (1,b"")
        assert command(sock,32,graph=graph,value=32,kind=1)== (10,b"")
        result=command(sock,32,graph=graph,value=32)
        alloc=object_id(result);allocation=struct.unpack("!Q",result[1][16:24])[0]
        assert command(sock,33,graph=graph,value=999)==(1,b"")
        assert command(sock,34,exec_id=999)==(2,b"")
        assert command(sock,35,graph=graph,value=1)==(1,b"")
        # A reserved graph address cannot bypass transport trust checks.
        assert call(sock,29,19,struct.pack("!QQIQ",allocation,0,0,4))==(1,b"")
        exec_id=object_id(command(sock,8,graph=graph))
        assert command(sock,34,exec_id=exec_id)[0]==0
        launch=command(sock,10,exec_id=exec_id)
        assert launch[0]==0,launch
        assert call(sock,4,20)==(0,b"")
        assert command(sock,2,graph=graph)[0]==0
        assert command(sock,9,exec_id=exec_id)[0]==0
        assert command(sock,10,exec_id=exec_id)==(2,b"")
        # The active allocation is held by the retired native exec until EOF cleanup.
    with socket.create_connection(endpoint,timeout=10) as sock:
        status,data=call(sock,89,21,struct.pack("!iI",0,0));assert status==0
        print("GRAPH_USED_AFTER_EOF_BEFORE_TRIM="+str(struct.unpack("!Q",data)[0]))
        # CoreX caches graph-pool accounting after freeing uploaded backing;
        # the native trim probe establishes the observable retirement boundary.
        assert call(sock,91,22,struct.pack("!i",0))==(0,b"")
        status,data=call(sock,89,23,struct.pack("!iI",0,0));assert status==0
        assert struct.unpack("!Q",data)[0]==0, ("graph_used_after_EOF",struct.unpack("!Q",data)[0])
        status,data=call(sock,12,22,struct.pack("!I",1));assert status==0
        stream=struct.unpack("!Q",data)[0]
        capture=object_id(command(sock,24,stream=stream,value=1))
        assert command(sock,35,graph=capture,value=4)==(1,b"")
        assert command(sock,35,graph=capture,value=1)[0]==0
        # Permission is consumed by exactly the next request, even a different opcode.
        assert command(sock,11,graph=capture)[0]==0
        assert call(sock,1,23,struct.pack("!Q",32))==(11,b"")
        assert command(sock,35,graph=capture,value=1)[0]==0
        status,data=call(sock,1,24,struct.pack("!Q",32));assert status==0
        assert object_id(command(sock,26,graph=capture,stream=stream))==capture
    print("GRAPH_EXPANSION_PROTOCOL=PASS new_actions DTO_bounds child_dependency_ownership supplied_graph_EOF")
