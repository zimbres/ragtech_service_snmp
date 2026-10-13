"""Exercise the native UDP backend; skip where the sandbox forbids sockets."""
import errno
import socket
import subprocess
import sys
import unittest
from test_snmp import request,decode,B,E
class UdpTests(unittest.TestCase):
    def test_native_udp_roundtrip(self):
        try:
            client=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
            client.bind(('127.0.0.1',0));client.settimeout(3)
            probe=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
            probe.bind(('127.0.0.1',0));port=probe.getsockname()[1];probe.close()
        except OSError as exc:
            if exc.errno in (errno.EPERM,errno.EACCES):self.skipTest('Environment forbids UDP sockets')
            raise
        server=None
        try:
            server=subprocess.Popen([sys.argv[1],'1','4',str(port)],stdout=subprocess.PIPE,text=True)
            self.assertEqual(server.stdout.readline().strip(),'ready')
            client.sendto(request([B+'2.5.0',B+'4.2.0',B+'4.4.1.3.1',E+'1.2.0']),('127.0.0.1',port))
            error,index,values=decode(client.recv(8192))
            self.assertEqual((error,index),(0,0))
            self.assertEqual([v[2] for v in values],[136,597,25,252])
            client.sendto(request(['1.3.6.1'],kind=0xa5,second=64),('127.0.0.1',port))
            values=decode(client.recv(8192))[2]
            self.assertEqual(len(values),53);self.assertEqual(values[-1][1],0x82)
        finally:
            client.close()
            if server:
                server.terminate();server.wait(timeout=5);server.stdout.close()
if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
