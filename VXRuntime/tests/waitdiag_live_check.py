#!/usr/bin/env python3
"""Real PE call/snapshot integration; no game files or game execution."""
import json, os, pathlib, select, subprocess, sys, time, unittest
BASE=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(BASE.parent/'tools'))
import vxrt_run as h
class Live(unittest.TestCase):
    def exercise(self, render=False):
        exe=BASE/'build'/('render_diagnostic.exe' if render else 'waitdiag_guest.exe')
        m=h.WindowMetadata(False,render,False,True)
        env=h.control_environment(dict(os.environ));env.update(VXRT_ROOT=str(BASE/'build/wait-diag-root'),VXRT_NO_WINE='1',VXRT_NO_ACTIVATE='1')
        m.prepare(env,str(exe));p=None
        try:
            p=subprocess.Popen([str(BASE/'build/vxrt'),str(exe)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            data=b'';deadline=time.monotonic()+20
            marker=b'Present PE calls passed' if render else b'waitdiag guest ready'
            while marker not in data and time.monotonic()<deadline:
                if select.select([p.stdout],[],[],.2)[0]:
                    chunk=os.read(p.stdout.fileno(),4096)
                    if not chunk:break
                    data+=chunk
            self.assertIn(marker,data,data.decode(errors='replace'))
            time.sleep(.05);m.remember_socket()
            rows=[m.sample(wait=True) for _ in range(3)]
            for row in rows:
                self.assertEqual(row['status'],'ok',row)
                value=row['wait'];self.assertEqual(value['status'],'active');self.assertFalse(value['overflow'])
                self.assertNotIn('SECRET_',json.dumps(value))
                self.assertTrue(any(t['active'] and t['frames'][0]['api']=='Sleep' and t['frames'][0]['caller_module']=='main_image' for t in value['threads']))
                if render:
                    self.assertGreater(value['last_present_thread'],0)
                    t=next(t for t in value['threads'] if t['thread']==value['last_present_thread'])
                    self.assertEqual(t['frames'][0]['api'],'Sleep')
                else:
                    blocked=[t for t in value['threads'] if t['active'] and t['frames'][0]['api']=='WaitForSingleObject']
                    self.assertEqual(len(blocked),1)
                    self.assertEqual(blocked[0]['frames'][0]['caller_module'],'main_image')
                    self.assertGreater(blocked[0]['frames'][0]['object'],0)
                    self.assertIn(blocked[0]['frames'][-1]['phase'],('objects_shared_address_wait','objects_shared_poll_wait','objects_check'))
            if not render:
                identities=[[(t['thread'],t['frames'][0]['started_ns']) for t in row['wait']['threads'] if t['active'] and t['frames'][0]['api']=='WaitForSingleObject'] for row in rows]
                self.assertEqual(identities[0],identities[1]);self.assertEqual(identities[1],identities[2])
        finally:
            if p:
                if p.poll() is None:p.terminate()
                p.wait(5);p.stdout.close()
            m.cleanup()
    def test_maximum_core_snapshot(self):
        raw=subprocess.check_output([str(BASE/'build/waitdiag_host'),'max'])
        self.assertLess(len(raw),131072)
        value=json.loads(raw)
        self.assertIsNotNone(h.validate_wait_snapshot(value))
        self.assertEqual(len(value['threads']),64)
    def test_d3d11_semantics_with_diagnostic(self):
        exe=BASE/'build/d3d11.exe';m=h.WindowMetadata(False,wait=True)
        env=h.control_environment(dict(os.environ));env.update(VXRT_ROOT=str(BASE/'build/wait-diag-root'),VXRT_NO_WINE='1',VXRT_NO_ACTIVATE='1')
        m.prepare(env,str(exe))
        try:
            p=subprocess.run([str(BASE/'build/vxrt'),str(exe)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            self.assertEqual(p.returncode,0,p.stdout.decode(errors='replace'))
        finally:m.cleanup()
    def test_wait_entered_before_first_snapshot(self):self.exercise()
    def test_last_present_thread(self):self.exercise(True)
if __name__=='__main__':unittest.main()
