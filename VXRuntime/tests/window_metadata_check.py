#!/usr/bin/env python3
"""No game: isolated PE and harness metadata protocol fixtures."""
import importlib.util, shutil, json, os, pathlib, socket, subprocess, sys, tempfile, threading, time, unittest
from unittest.mock import patch
BASE=pathlib.Path(__file__).resolve().parents[1]
REPO=BASE.parent
sys.path.insert(0,str(REPO/'tools'))
import vxrt_run as harness
EXE=BASE/'build/window_metadata.exe'
VXRT=BASE/'build/vxrt'
class Runtime(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(prefix='vxclip-',dir='/private/tmp')
        self.path=pathlib.Path(self.directory.name)/'control.sock'
        self.processes=[]
        self.env=harness.control_environment(dict(os.environ))
        self.env.update(VXRT_ROOT=str(BASE/'build/window-metadata-root'),VXRT_NO_WINE='1',VXRT_NO_ACTIVATE='1',VXRT_CONTROL_DIAG='metadata',VXRT_CONTROL_TARGET=str(EXE.resolve()),VXRT_CONTROL_DIAG_SOCKET=str(self.path))
    def tearDown(self):
        for p in self.processes:
            if p.poll() is None:p.terminate()
            p.wait(timeout=5)
            self.assertNotIn(b"SECRET_",p.stdout.read())
            p.stdout.close()
        self.directory.cleanup()
    def launch(self,env=None,duration=15000,wait=True):
        p=subprocess.Popen([str(VXRT),str(EXE),str(duration)],env=env or self.env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.processes.append(p)
        if wait:
            deadline=time.monotonic()+4
            while not self.path.exists() and p.poll() is None and time.monotonic()<deadline:time.sleep(.02)
            self.assertTrue(self.path.exists())
        return p
    def ask(self,command):
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
            s.settimeout(3);s.connect(str(self.path));s.sendall(command+b'\n');data=b''
            while b'\n' not in data:
                b=s.recv(512)
                if not b:break
                data+=b
        self.assertNotIn(b'SECRET_',data)
        return json.loads(data)
    def test_metadata_and_limit(self):
        self.launch();time.sleep(.2)
        self.assertEqual(self.path.stat().st_mode & 0o777,0o600)
        for i in range(3):
            r=self.ask(b'metadata');self.assertEqual(r['status'],'ok');self.assertGreaterEqual(r['total_windows'],2)
            self.assertEqual(set(r),{'schema_version','status','pid','total_windows','visible_windows','enabled_windows','child_windows','minimized_windows','error_category'})
        time.sleep(.1);self.assertFalse(self.path.exists())
    def test_actions_and_text_rejected(self):
        self.launch()
        self.assertEqual(self.ask(b'type 0 SECRET_INPUT_91827'),{'status':'rejected'})
        self.assertEqual(self.ask(b'state'),{'status':'rejected'})
        self.assertEqual(self.ask(b'metadata')['status'],'ok')
    def test_timeout(self):
        self.launch()
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
            s.settimeout(3);s.connect(str(self.path));t=time.monotonic();s.sendall(b'm')
            self.assertEqual(s.recv(100),b'');self.assertLess(time.monotonic()-t,2.8)
        self.assertEqual(self.ask(b'metadata')['status'],'ok')
    def test_identity_default_and_child(self):
        for change in ({'VXRT_CHILD':'1'},{'VXRT_CONTROL_TARGET':'/nonexistent'},{'VXRT_CONTROL_DIAG':'off'}):
            env=dict(self.env,**change);p=self.launch(env,duration=200,wait=False);self.assertEqual(p.wait(5),0);self.assertFalse(self.path.exists())
        env=harness.control_environment(self.env);p=self.launch(env,duration=200,wait=False);self.assertEqual(p.wait(5),0);self.assertFalse(self.path.exists())
    def test_existing_and_replacement(self):
        self.path.write_text('sentinel');p=self.launch(duration=200);self.assertEqual(p.wait(5),0);self.assertEqual(self.path.read_text(),'sentinel');self.path.unlink()
        p=self.launch(duration=1200);original=self.path.with_name('original.sock');self.path.rename(original);self.path.write_text('replacement')
        self.assertEqual(p.wait(5),0);self.assertEqual(self.path.read_text(),'replacement')
    def test_permissions(self):
        os.chmod(self.directory.name,0o755)
        p=self.launch(duration=200,wait=False);self.assertEqual(p.wait(5),0);self.assertFalse(self.path.exists())
        os.chmod(self.directory.name,0o700)
    def test_alias_and_existing_socket(self):
        copy=pathlib.Path(self.directory.name)/EXE.name;shutil.copyfile(EXE,copy)
        p=self.launch(dict(self.env,VXRT_CONTROL_TARGET=str(copy)),duration=200,wait=False)
        self.assertEqual(p.wait(5),0);self.assertFalse(self.path.exists())
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as old:
            old.bind(str(self.path));identity=self.path.stat().st_ino
            p=self.launch(duration=200);self.assertEqual(p.wait(5),0);self.assertEqual(self.path.stat().st_ino,identity)
    def test_symlink_parent(self):
        alias=pathlib.Path(self.directory.name+'-alias');alias.symlink_to(self.directory.name)
        try:
            env=dict(self.env,VXRT_CONTROL_DIAG_SOCKET=str(alias/'control.sock'))
            p=self.launch(env,duration=200,wait=False);self.assertEqual(p.wait(5),0);self.assertFalse(self.path.exists())
        finally:alias.unlink()
    def test_sampler_seal(self):
        self.launch();time.sleep(.2)
        m=harness.WindowMetadata(True);m.path=str(self.path);m.directory=self.directory.name
        st=os.lstat(m.directory);m.directory_identity=(st.st_dev,st.st_ino);m.schedule=(0,0,0)
        t=threading.Thread(target=m.collect,args=(threading.Event(),time.monotonic()));t.start();t.join(8);self.assertFalse(t.is_alive())
        ev=harness.Evidence()
        for i in range(19):ev.observe("existing_%d"%i,i)
        m.seal(self.directory.name,ev)
        self.assertEqual(ev.observed[-1]["id"],"o20")
        self.assertEqual(ev.observed[-1]["sha256"],harness.ledger.sha256_file(str(pathlib.Path(self.directory.name)/"window_metadata.jsonl")))
        self.assertEqual(m.config['request_count'],3);self.assertEqual(m.config['success_count'],3)
        self.assertNotIn('SECRET_',pathlib.Path(self.directory.name,'window_metadata.jsonl').read_text())
class Harness(unittest.TestCase):
    def test_off_and_environment(self):
        env=harness.control_environment({'VXRT_CONTROL':'secret','VXRT_CONTROL_LOG':'secret','VXRT_CONTROL_TARGET':'secret','OK':'yes'})
        self.assertEqual(env,{'OK':'yes'})
        m=harness.WindowMetadata(False)
        with patch.object(harness.tempfile,'mkdtemp',side_effect=AssertionError):m.prepare(env,'unused')
        self.assertIsNone(m.path)
    def test_refused_no_sampling(self):
        spec={'appid':'1','launcher':'game','scenarios':{'start':{}},'default_scenario':'start'}
        with tempfile.TemporaryDirectory() as tmp, patch.object(harness,'REPO',tmp), patch.object(harness,'resolve',return_value=('fixture',spec,{'exe':str(EXE)})), patch.object(harness,'git_state',return_value=('test',True)), patch.object(harness.doctor,'machine_info',return_value={}), patch.object(harness,'preconditions',return_value=[{'code':'fixture_refused','ok':False}]), patch.object(harness,'finish',side_effect=lambda r,*a:r), patch.object(harness.WindowMetadata,'prepare',side_effect=AssertionError):
            r=harness.run('fixture',window_metadata=True);self.assertEqual(r['status'],'refused');self.assertEqual(r['window_diagnostic']['request_count'],0)
    def test_cleanup_permission_failure_keeps_process_cleanup_possible(self):
        m=harness.WindowMetadata(True);m.path='/private/tmp/vxclip-fixture/control.sock';m.directory='/private/tmp/vxclip-fixture'
        with patch.object(m,'owns_directory',return_value=True), patch.object(harness.os,'lstat',side_effect=PermissionError(13,'denied')):
            m.cleanup()
    def test_stop_before_sample(self):
        m=harness.WindowMetadata(True);stop=threading.Event();stop.set()
        with patch.object(m,'sample',side_effect=AssertionError):m.collect(stop,time.monotonic())
        self.assertEqual(m.rows,[])
if __name__=='__main__':unittest.main()
