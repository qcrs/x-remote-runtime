#!/usr/bin/env python3
import os,socket,struct
from function_occupancy_protocol import call
port=int(os.environ['COREX_REMOTE_PORT'])
with socket.create_connection((os.environ.get('COREX_REMOTE_HOST','127.0.0.1'),port),timeout=10) as sock:
    status,body=call(sock,1,1,struct.pack('!Q',384))
    assert status==0 and len(body)==8
    allocation=struct.unpack('!Q',body)[0]
    def fill(**kwargs):
        fields=dict(allocation=allocation,offset=0,pitch=32,slice=128,width=17,height=2,depth=3,value=1)
        fields.update(kwargs)
        return struct.pack('!7QI',*fields.values())
    checks=[(65,b'bad',1),(65,fill(depth=0),1),
            (65,fill(offset=2**64-1),1),(65,fill(slice=2**64-1),1),
            (65,fill(depth=2**64-1),1),(65,fill(value=256),1),
            (66,fill()+struct.pack('!Q',999),1),
            (67,struct.pack('!Qi',999,0),2),(68,struct.pack('!QiQI',999,128,0,0),2),
            (69,struct.pack('!QQi',999,0,0),2),(69,b'bad',1)]
    for request,(op,payload,want) in enumerate(checks,2):
        status,body=call(sock,op,request,payload)
        assert (status,body)==(want,b''),(op,status,body)
    status,body=call(sock,65,20,fill())
    assert (status,body)==(0,b'')
    status,body=call(sock,6,21,struct.pack('!Q',allocation))
    assert (status,body)==(0,b'')
    call(sock,7,22)
print('EXECUTION_FILL_PROTOCOL=PASS')
