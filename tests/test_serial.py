"""Exercise Linux termios2 and fragmented register reads through a real pseudo-TTY."""
import errno
import os
import select
import subprocess
import sys
import time
import unittest
class SerialTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform.startswith('linux'),'Pseudo-TTY emulator requires Linux')
    def test_native_serial_and_fragmented_calibration(self):
        try:master,slave=os.openpty()
        except OSError as exc:
            if exc.errno in (errno.EPERM,errno.EACCES):self.skipTest('Environment forbids pseudo-TTYs')
            raise
        process=None
        try:
            process=subprocess.Popen([sys.argv[1],os.ttyname(slave)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            self.assertEqual(process.stdout.readline().strip(),'ready')
            os.close(slave);slave=None
            packet=bytes.fromhex('AA 21 00 0C 40 00 00 00 FF 00 DE CA 72 1F 20 2F FB 00 00 0B 86 C2 01 98 2E 00 72 02 0E 00 C6')
            exchanges=[(bytes.fromhex('AA0400F301F4'),b'\xaa\x35'),
                       (bytes.fromhex('AA0402020206'),b'\xaa\x1e\x28'),
                       (bytes.fromhex('AA0400801E9E'),packet)]
            for command,response in exchanges:
                received=b'';deadline=time.monotonic()+3
                while len(received)<6:
                    remaining=deadline-time.monotonic()
                    self.assertGreater(remaining,0,'Timed out waiting for serial command')
                    ready,_,_=select.select([master],[],[],remaining)
                    self.assertTrue(ready)
                    received+=os.read(master,6-len(received))
                self.assertEqual(received,command)
                for byte in response:
                    os.write(master,bytes([byte]));time.sleep(.005)
                self.assertEqual(process.stdout.readline().strip(),response.hex().upper())
            self.assertEqual(process.wait(timeout=3),0,process.stderr.read())
        finally:
            os.close(master)
            if slave is not None:os.close(slave)
            if process:
                if process.poll() is None:process.kill();process.wait(timeout=3)
                process.stdout.close();process.stderr.close()
if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
