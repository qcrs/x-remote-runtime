#!/usr/bin/env python3
import os,socket,struct,sys
from function_occupancy_protocol import call
image=open(sys.argv[1],'rb').read()
name=b'symbol.cubin'
image_payload=struct.pack('!I',len(name))+name+struct.pack('!Q',len(image))+image
endpoint=('127.0.0.1',int(os.environ['COREX_REMOTE_PORT']))
def query_payload(module,name):return struct.pack('!QI',module,len(name))+name
with socket.create_connection(endpoint,timeout=10) as sock:
    status,body=call(sock,8,1,image_payload);assert status==0 and len(body)==8
    module=struct.unpack('!Q',body)[0]
    query=query_payload(module,b'runtime_symbol_probe')
    status,body=call(sock,73,2,query);assert status==0 and len(body)==16,(status,body)
    allocation,bytes_=struct.unpack('!QQ',body);assert bytes_==4
    assert call(sock,73,3,query)==(0,body) # stable identity in the existing allocation table
    for req,payload,want in [(4,b'bad',1),(5,query_payload(999,b'x'),2),
          (6,query_payload(module,b''),1),(7,query_payload(module,b'x'*128),1),
          (8,query_payload(module,b'x\0y'),1),(9,query_payload(module,b'no_such_symbol'),9)]:
        result=call(sock,73,req,payload);assert result==(want,b''),(req,result)
    assert call(sock,6,10,struct.pack('!Q',allocation))==(1,b'') # native storage belongs to the module
    status,body=call(sock,5,11,struct.pack('!QQQ',allocation,0,4))
    assert status==0 and struct.unpack('=i',body)==(41,),(status,body)
    assert call(sock,10,12,struct.pack('!Q',module))==(0,b'')
    assert call(sock,73,13,query)==(2,b'')
    assert call(sock,5,14,struct.pack('!QQQ',allocation,0,4))==(2,b'')
    call(sock,7,15)
# EOF cleanup must unload module storage without cuMemFree on its aliases.
with socket.create_connection(endpoint,timeout=10) as sock:
    status,body=call(sock,8,1,image_payload);assert status==0
    module=struct.unpack('!Q',body)[0]
    status,body=call(sock,73,2,query_payload(module,b'runtime_constant_probe'));assert status==0 and len(body)==16
# A response in the next session proves the preceding EOF cleanup completed.
with socket.create_connection(endpoint,timeout=10) as sock:
    status,body=call(sock,27,1);assert status==0 and len(body)>0
    call(sock,7,2)
print('SYMBOL_PROTOCOL=PASS bounded_DTO unknown_module missing_symbol nonowning_free stale_alias EOF_cleanup')
