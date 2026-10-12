#!/usr/bin/env python3
import os,socket,struct,sys
from function_occupancy_protocol import call
image=open(sys.argv[1],'rb').read()
name=b'library.cubin'
payload=struct.pack('!I',len(name))+name+struct.pack('!Q',len(image))+image
with socket.create_connection(('127.0.0.1',int(os.environ['COREX_REMOTE_PORT'])),timeout=10) as sock:
    status,body=call(sock,70,1,payload)
    assert status==0 and len(body)==8,(status,body)
    module=struct.unpack('!Q',body)[0]
    for req,(op,p,want) in enumerate([(71,b'bad',1),(71,struct.pack('!Q',999),2),
          (72,struct.pack('!QI',module,999),1),(72,b'bad',1)],2):
        assert call(sock,op,req,p)==(want,b'')
    status,body=call(sock,71,10,struct.pack('!Q',module))
    assert status==0 and struct.unpack('!I',body)==(2,)
    status,body=call(sock,72,11,struct.pack('!QI',module,0))
    assert status==0 and 0<len(body)<=131
    length=struct.unpack('!I',body[:4])[0];assert length==len(body)-4
    assert body[4:] in [b'm1s2_lifecycle_kernel',b'library_mixed_kernel']
    assert call(sock,10,12,struct.pack('!Q',module))==(0,b'')
    assert call(sock,71,13,struct.pack('!Q',module))==(2,b'')
    call(sock,7,14)
# EOF cleanup with an outstanding native library exercises server ownership.
with socket.create_connection(('127.0.0.1',int(os.environ['COREX_REMOTE_PORT'])),timeout=10) as sock:
    status,body=call(sock,70,1,payload);assert status==0 and len(body)==8
print('LIBRARY_EXPANSION_PROTOCOL=PASS')
