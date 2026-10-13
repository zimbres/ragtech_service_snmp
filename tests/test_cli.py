"""Portable CLI/config checks that do not open the UPS or bind UDP ports."""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
EXE=sys.argv[1]
class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.path=Path(self.temp.name)/'ragtech.conf'
        self.path.write_text('[serial]\nport=COM3\nbaud_rate=2560\n[general]\ndaemonize=false\npid_file=\n')
    def tearDown(self):self.temp.cleanup()
    def run_cli(self,*args):return subprocess.run([EXE,*args],capture_output=True,text=True,timeout=5)
    def test_help_version(self):
        for option in ('-h','--help','-v','--version'):
            self.assertEqual(self.run_cli(option).returncode,0)
    def test_config_short_long_equals(self):
        for args in [('-c',str(self.path),'-t'),('--config',str(self.path),'--test-config'),('--config='+str(self.path),'-f','-t','--log-level=debug')]:
            result=self.run_cli(*args);self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('state_directory',result.stdout)
    def test_attached_and_bundled_options(self):
        for args in [('-c'+str(self.path),'-ft'),('-c',str(self.path),'-fl','debug','-t'),
                     ('-fc'+str(self.path),'-tlerror')]:
            result=self.run_cli(*args);self.assertEqual(result.returncode,0,result.stderr)
    def test_foreground_overrides_config(self):
        with self.path.open('a') as f:f.write('daemonize=true\n')
        self.assertEqual(self.run_cli('-c',str(self.path),'-f','-t').returncode,0)
    def test_invalid_options_and_serial(self):
        for args in [('-c',),('--config=',),('--unknown',),('-c',str(self.path),'-l','invalid','-t')]:
            self.assertNotEqual(self.run_cli(*args).returncode,0)
        for setting in ['baud_rate=0','data_bits=9','stop_bits=3','parity=X']:
            self.path.write_text('[serial]\n'+setting+'\n')
            self.assertNotEqual(self.run_cli('-c',str(self.path),'-t').returncode,0)
    @unittest.skipUnless(os.name=='nt','Windows-specific daemon rejection')
    def test_windows_rejects_daemon_and_pid(self):
        for args in [('-d',),('-p',str(Path(self.temp.name)/'pid'))]:
            result=self.run_cli('-c',str(self.path),'-t',*args)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('Windows runs in foreground',result.stderr)
if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
