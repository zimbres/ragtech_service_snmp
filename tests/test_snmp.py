"""Independent BER client exercising agent packets with a synthetic UPS snapshot."""
import subprocess
import sys
import unittest

B = '1.3.6.1.2.1.33.1.'
E = '1.3.6.1.3.1628.1.'
def tlv(tag, data):
    n=len(data)
    size=bytes([n]) if n<128 else bytes([0x82,n>>8,n&255])
    return bytes([tag])+size+data
def integer(n):
    data=n.to_bytes(4,'big',signed=True)
    while len(data)>1 and ((data[0]==0 and data[1]<128) or (data[0]==255 and data[1]>=128)):
        data=data[1:]
    return tlv(2,data)
def arc(n):
    data=bytes([n&127]);n>>=7
    while n:
        data=bytes([(n&127)|128])+data;n>>=7
    return data
def oid(name):
    a=list(map(int,name.split('.')))
    return tlv(6,arc(a[0]*40+a[1])+b''.join(arc(x) for x in a[2:]))
def read(data):
    tag,n=data[:2];pos=2
    if n&128:
        count=n&127;n=int.from_bytes(data[pos:pos+count],'big');pos+=count
    assert len(data)>=pos+n
    return tag,data[pos:pos+n],data[pos+n:]
def decode_oid(data):
    a=[];n=0
    for c in data:
        n=n*128+(c&127)
        if not c&128:
            if not a:
                first=min(n//40,2);a=[first,n-first*40]
            else:a.append(n)
            n=0
    return '.'.join(map(str,a))
def request(names,kind=0xa0,first=0,second=0,community=b'public',version=1):
    variables=b''.join(tlv(0x30,oid(n)+tlv(5,b'')) for n in names)
    pdu=integer(9384)+integer(first)+integer(second)+tlv(0x30,variables)
    return tlv(0x30,integer(version)+tlv(4,community)+tlv(kind,pdu))
def decode(packet):
    tag,msg,rest=read(packet);assert tag==0x30 and not rest
    _,_,msg=read(msg);_,community,msg=read(msg)
    tag,pdu,rest=read(msg);assert tag==0xa2 and not rest
    _,rid,pdu=read(pdu);assert int.from_bytes(rid,'big',signed=True)==9384
    _,error,pdu=read(pdu);_,index,pdu=read(pdu);_,variables,rest=read(pdu);assert not rest
    values=[]
    while variables:
        _,entry,variables=read(variables);_,name,entry=read(entry);tag,value,rest=read(entry);assert not rest
        values.append((decode_oid(name),tag,int.from_bytes(value,'big',signed=True) if tag==2 else value))
    return int.from_bytes(error,'big'),int.from_bytes(index,'big'),values

class AgentTests(unittest.TestCase):
    def setUp(self):
        self.start()
    def start(self,connected=1,mode=0):
        self.process=subprocess.Popen([sys.argv[1],str(connected),str(mode)],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
    def tearDown(self):
        self.process.stdin.close();self.process.wait(timeout=3);self.process.stdout.close()
    def exchange(self,packet):
        self.process.stdin.write(packet.hex()+'\n');self.process.stdin.flush()
        return bytes.fromhex(self.process.stdout.readline().strip())
    def query(self,names,**kwargs):
        return decode(self.exchange(request(names,**kwargs)))
    def test_required_and_units(self):
        names=['1.1.0','1.2.0','4.1.0','2.1.0','2.2.0','2.3.0','2.4.0','4.4.1.5.1']
        error,index,values=self.query([B+x for x in names])
        self.assertEqual((error,index),(0,0))
        self.assertEqual([v[2] for v in values],[b'Ragtech',b'EASY 700 TI',3,2,0,12,75,42])
        self.assertEqual([v[1] for v in values],[4,4,2,2,2,2,2,2])
        values=self.query([B+x for x in ['2.5.0','4.2.0','4.4.1.3.1','4.4.1.4.1']])[2]
        self.assertEqual([v[2] for v in values],[123,598,25,300])
    def test_complete_agent_version(self):
        values=self.query([B+'1.4.0'])[2]
        self.assertEqual(values,[(B+'1.4.0',4,b'ragtech_service 3.0.0')])
    def test_proprietary_sample(self):
        self.tearDown();self.start(mode=4)
        values=self.query([B+x for x in ['2.5.0','4.2.0','4.4.1.3.1','4.4.1.4.1','4.4.1.5.1']])[2]
        self.assertEqual([v[2] for v in values],[136,597,25,276,32])
        values=self.query([E+'1.11.0',E+'1.12.0',E+'1.2.0',E+'1.5.0'])[2]
        self.assertEqual([v[2] for v in values],[1360,5971,252,27638])
    def test_frequency_without_calibration(self):
        self.tearDown();self.start(mode=5)
        values=self.query([B+'4.2.0',E+'1.12.0',B+'2.5.0'])[2]
        self.assertEqual([v[1] for v in values],[0x81,0x81,2])
        self.assertEqual(values[2][2],123)
    def test_battery_states(self):
        for mode,status in [(1,2),(2,3),(3,4)]:
            self.tearDown();self.start(mode=mode)
            values=self.query([B+'4.1.0',B+'2.1.0',B+'2.2.0'])[2]
            self.assertEqual([v[2] for v in values],[5,status,123])
    def test_walk_and_extensions(self):
        names=[];current='1.3.6.1'
        for _ in range(100):
            value=self.query([current],kind=0xa1)[2][0]
            if value[1]==0x82:break
            self.assertGreater(tuple(map(int,value[0].split('.'))),tuple(map(int,current.split('.'))))
            names.append(value[0]);current=value[0]
        else:self.fail('walk failed to terminate')
        self.assertEqual(len(names),52)
        self.assertTrue(all(E+f'1.{i}.0' in names for i in range(1,14)))
        self.assertTrue(all(E+f'2.{i}.0' in names for i in range(1,21)))
        self.assertEqual(self.query([E+'1.7.0',E+'3.0',E+'4.0'])[2],
                         [(E+'1.7.0',2,120),(E+'3.0',2,1),(E+'4.0',4,b'\xaa\x80')])
        values=self.query(['1.3.6.1'],kind=0xa5,second=64)[2]
        self.assertEqual([v[0] for v in values[:-1]],names)
        self.assertEqual(values[-1][1],0x82)
    def test_bulk_nonrepeaters_and_limits(self):
        values=self.query([B+'1',B+'2'],kind=0xa5,first=1,second=2)[2]
        self.assertEqual([v[0] for v in values],[B+'1.1.0',B+'2.1.0',B+'2.2.0'])
        self.assertEqual(len(self.query([B+'1'],kind=0xa5,second=0)[2]),0)
        self.assertLessEqual(len(self.query([B+'1']*10,kind=0xa5,second=2147483647)[2]),64)
    def test_exceptions_and_readonly(self):
        values=self.query([B+'99.0',B+'1.1',B+'1.1.2'])[2]
        self.assertEqual([v[1] for v in values],[0x80,0x81,0x81])
        error,index,values=self.query([B+'2.4.0'],kind=0xa3)
        self.assertEqual((error,index),(17,1));self.assertEqual(values[0][1],5)
    def test_invalid_packets(self):
        packets=[request([B+'1.1.0'],community=b'wrong'),request([B+'1.1.0'],version=0),
                 b'\x30\x80',b'\x30\x82\xff\xff',request([B+'1.1.0'])+b'junk',b'x'*8193]
        good=request([B+'1.1.0'])
        packets.extend(good[:i] for i in range(len(good)))
        for packet in packets:
            self.assertEqual(self.exchange(packet),b'')
        self.assertEqual(self.query([B+'1.1.0'])[2][0][2],b'Ragtech')
    def test_disconnected(self):
        self.tearDown();self.start(connected=0)
        values=self.query([B+'4.1.0',B+'2.1.0',E+'3.0'])[2]
        self.assertEqual([v[2] for v in values],[1,1,0])
        values=self.query([B+'2.3.0',B+'2.4.0',B+'4.4.1.5.1'])[2]
        self.assertEqual([v[1] for v in values],[0x81]*3)

if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
