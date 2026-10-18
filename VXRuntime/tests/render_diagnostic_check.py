#!/usr/bin/env python3
"""Real PE/COM rendering fixtures; no game or texture capture."""
import json, os, pathlib, select, subprocess, sys, tempfile, threading, time, unittest
from unittest.mock import patch
BASE=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(BASE.parent/'tools'))
import vxrt_run as h
EXE=BASE/'build/render_diagnostic.exe'
class Render(unittest.TestCase):
    def exercise(self, windows, render, excluded=None):
        m=h.WindowMetadata(windows,render)
        env=h.control_environment(dict(os.environ));env.update(VXRT_ROOT=str(BASE/'build/render-diag-root'),VXRT_NO_WINE='1',VXRT_NO_ACTIVATE='1')
        m.prepare(env,str(EXE));p=None
        if excluded:env.update(excluded)
        try:
            p=subprocess.Popen([str(BASE/'build/vxrt'),str(EXE)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            deadline=time.monotonic()+15;lines=b''
            while b'real device/swapchain/Present PE calls passed' not in lines and time.monotonic()<deadline:
                if select.select([p.stdout],[],[],.2)[0]:
                    b=os.read(p.stdout.fileno(),4096)
                    if not b:break
                    lines+=b
            self.assertIn(b'real device/swapchain/Present PE calls passed',lines,lines.decode(errors='replace'))
            self.assertNotIn(b'SECRET_',lines)
            if not render or excluded:
                if excluded:self.assertFalse(os.path.exists(m.path))
                else:self.assertIsNone(m.path)
                return
            m.schedule=(0,0,0);t=threading.Thread(target=m.collect,args=(threading.Event(),time.monotonic()));t.start();t.join(7);self.assertFalse(t.is_alive())
            self.assertEqual(m.render_config['request_count'],3);self.assertEqual(m.render_config['success_count'],3)
            for row in m.rows:
                self.assertEqual('total_windows' in row,windows)
                r=row['render'];self.assertEqual(r['openvr_submit'],'not_instrumented')
                apis=r['apis'];self.assertEqual([a['entered'] for a in apis],[3,1,2,1,3,1])
                self.assertEqual([a['completed'] for a in apis],[3,1,2,1,3,1])
                self.assertEqual(apis[0]['last_hresult'],0x80070057);self.assertEqual(apis[0]['failure'],1);self.assertEqual(apis[0]['success'],2)
                self.assertEqual(apis[2]['failure'],1)
                self.assertEqual(r['present_test'],2)
                self.assertEqual(r['present_no_drawable']+r['present_drawable_committed'],2)
                self.assertEqual(r['d3d12_delegate'],0)
            print("render stages: test=%d no_drawable=%d committed=%d" % tuple(m.rows[-1]['render'][k] for k in ('present_test','present_no_drawable','present_drawable_committed')))
            with tempfile.TemporaryDirectory() as tmp:
                ev=h.Evidence();m.seal(tmp,ev)
                self.assertEqual(len(ev.observed),2 if windows else 1)
                for o in ev.observed:
                    payload=pathlib.Path(BASE.parent/o['artifact']).read_text()
                    self.assertNotIn('SECRET_',payload)
                    self.assertEqual(o['sha256'],h.ledger.sha256_file(str(BASE.parent/o['artifact'])))
            time.sleep(.1);self.assertFalse(os.path.exists(m.path))
        finally:
            if p:
                if p.poll() is None:p.terminate()
                p.wait(5);p.stdout.close()
            m.cleanup()
    def test_render_only(self):self.exercise(False,True)
    def test_combined_single_budget(self):self.exercise(True,True)
    def test_default_off_real_calls(self):self.exercise(False,False)
    def test_child_and_other_target(self):
        self.exercise(False,True,{"VXRT_CHILD":"1"})
        self.exercise(False,True,{"VXRT_CONTROL_TARGET":sys.executable})
    def test_activity_real_pe(self):
        exe=BASE/'build/activity_diagnostic.exe'
        m=h.WindowMetadata(False,True)
        env=h.control_environment(dict(os.environ));env.update(VXRT_ROOT=str(BASE/'build/render-diag-root'),VXRT_NO_WINE='1',VXRT_NO_ACTIVATE='1')
        m.prepare(env,str(exe));p=None
        try:
            p=subprocess.Popen([str(BASE/'build/vxrt'),str(exe)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            data=b'';rows=[];deadline=time.monotonic()+25
            while len(rows)<3 and time.monotonic()<deadline:
                if select.select([p.stdout],[],[],.2)[0]:
                    chunk=os.read(p.stdout.fileno(),4096)
                    if not chunk:break
                    data+=chunk
                    marker=('activity phase %d' % (len(rows)+1)).encode()
                    if marker in data:
                        row=m.sample();self.assertEqual(row['status'],'ok');rows.append(row['render'])
            self.assertEqual(len(rows),3,data.decode(errors='replace'))
            self.assertNotIn(b'SECRET_',data)
            self.assertEqual([r['work_commit_main'] for r in rows],[0,1,1])
            self.assertEqual([r['work_commit_pre'] for r in rows],[0,0,0])
            self.assertEqual([r['window_callback_entered'] for r in rows],[0,3,3])
            self.assertEqual([r['window_callback_completed'] for r in rows],[0,3,3])
            self.assertTrue(all(r['process_cpu_valid'] for r in rows))
            self.assertEqual(sorted(r['process_cpu_ns'] for r in rows),[r['process_cpu_ns'] for r in rows])
            p.wait(7);self.assertEqual(p.returncode,0)
        finally:
            if p:
                if p.poll() is None:p.terminate()
                p.wait(5);p.stdout.close()
            m.cleanup()
    def test_maximum_json_and_cpu_unavailable(self):
        raw=subprocess.check_output([str(BASE/'build/renderdiag_host'),'--max'])
        value=json.loads(raw);self.assertIsNotNone(h.validate_render_counters(value))
        self.assertLess(len(raw)+512,4096)
        self.assertEqual(value['process_cpu_ns'],2**64-1)
        value.update(process_cpu_ns=None,process_cpu_valid=False)
        self.assertIsNotNone(h.validate_render_counters(value))
        value['process_cpu_ns']=0;self.assertIsNone(h.validate_render_counters(value))
        value.update(process_cpu_ns=None,process_cpu_valid=True)
        self.assertIsNone(h.validate_render_counters(value))
        value.update(process_cpu_ns=1,process_cpu_valid=True,secret='SECRET_NOT_LOGGED')
        value['apis'][0]['secret']='SECRET_NOT_LOGGED'
        self.assertNotIn('SECRET',json.dumps(h.validate_render_counters(value)))
    def test_invalid_and_secret_fields(self):
        self.assertIsNone(h.validate_render_counters({'secret':'SECRET_NOT_LOGGED'}))
        m=h.WindowMetadata(False,True);self.assertFalse(m.config['enabled']);self.assertTrue(m.render_config['enabled'])
        self.assertEqual(m.render_config['openvr_submit'],'not_instrumented')
if __name__=='__main__':unittest.main()
