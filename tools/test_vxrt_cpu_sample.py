#!/usr/bin/env python3
"""Only self-owned fixtures; no games or external process sampling."""
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import vxrt_cpu_sample as cpu
import vxrt_run as harness
BASE = Path(__file__).resolve().parents[1] / 'VXRuntime'

class Validation(unittest.TestCase):
    def test_bounds_and_payloads(self):
        good = dict(status='complete', elapsed_ms=2004, samples=2, overflow=False,
                    unknown=1, unpublished=0, other=0, dropped=0,
                    hotspots=[dict(module='runtime', offset=42, weight=1)])
        self.assertEqual(cpu.validate(good), good)
        for module in ("vrclient_x64", "openvr_api", "msvcr100", "msvcp100", "ucrtbase", "vcruntime140", "msvcp140", "system_kernel", "system_pthread", "system_c", "system_dispatch", "system_objc"):
            known=copy.deepcopy(good);known["hotspots"][0]["module"]=module;self.assertEqual(cpu.validate(known),known)
        bads = [None, [], dict(status=[]), dict(status='SECRET_token')]
        for k, v in [('elapsed_ms',2251),('samples',129),('unknown',True),('overflow',1),('hotspots',[{'module':'SECRET_path'}])]:
            b=copy.deepcopy(good);b[k]=v;bads.append(b)
        b=copy.deepcopy(good);b['hotspots']*=33;bads.append(b)
        b=copy.deepcopy(good);b['hotspots'][0]['offset']=-1;bads.append(b)
        b=copy.deepcopy(good);b['hotspots'][0]['weight']=2;bads.append(b)
        for b in bads:self.assertIsNone(cpu.validate(b))
        b=copy.deepcopy(good);b['SECRET']='path';b['hotspots'][0]['name']='SECRET';self.assertNotIn('SECRET',json.dumps(cpu.validate(b)))
    def test_default_and_required_metadata(self):
        with self.assertRaises(ValueError):harness.WindowMetadata(False,cpu=True)
        m=harness.WindowMetadata(False);env={'VXRT_CPU_DIAG':'1'};m.prepare(env,'unused');self.assertNotIn('VXRT_CPU_DIAG',env)
        self.assertFalse(m.cpu.enabled)
    def test_identity_and_failed_first(self):
        c=cpu.CpuDiagnostic(True);c.collect(None,{'status':'ok','pid':1},1)
        c.collect(None,{'status':'ok','pid':2},2);self.assertEqual(c.value['status'],'identity_mismatch')
        c=cpu.CpuDiagnostic(True);c.collect(None,{'status':'unavailable'},1);c.collect(None,{},2)
        self.assertEqual(c.value['status'],'target_unavailable')
    def test_transport_failures_are_fixed(self):
        c=cpu.CpuDiagnostic(True);c.collect(None,{'status':'ok','pid':1},1)
        class Endpoint:
            path='/not-used'
            socket_identity=(1,2)
            def owns_directory(self): return True
        with patch.object(cpu.os,'lstat',side_effect=PermissionError('SECRET')):
            c.collect(Endpoint(),{'status':'ok','pid':1},2)
        self.assertEqual(c.value,{'status':'invalid_response'})
        self.assertNotIn('SECRET',json.dumps(c.value))

    def test_seal_append(self):
        c=cpu.CpuDiagnostic(True);c.value={'status':'conflict'};e=harness.Evidence();e.observe('old',1)
        with tempfile.TemporaryDirectory() as d:
            c.seal(d,e);self.assertEqual(e.observed[-1]['id'],'o2');self.assertEqual(e.observed[-1]['metric'],'cpu_diagnostic')

class Native(unittest.TestCase):
    def test_modes(self):
        for mode in ('off','handler','timer','profile','busy'):
            p=subprocess.run([str(BASE/'build/cpudiag_host'),mode],capture_output=True,timeout=6)
            self.assertEqual(p.returncode,0,mode)
            self.assertNotIn(b'SECRET',p.stdout)
            if mode=='busy':
                v=cpu.validate(json.loads(p.stdout));self.assertIsNotNone(v);self.assertEqual(v['status'],'complete')
                self.assertLess(len(p.stdout),3072)

class Runtime(unittest.TestCase):
    def test_child_and_wrong_target(self):
        exe=BASE/'build/cpudiag_guest.exe'
        for change in ({'VXRT_CHILD':'1'}, {'VXRT_CONTROL_TARGET':'/not-the-main.exe'}):
            m=harness.WindowMetadata(True,cpu=True)
            env=harness.control_environment(dict(os.environ))
            env.update(VXRT_ROOT=str(BASE/'build/cpudiag-root'),VXRT_NO_WINE='1',VXRT_NO_ACTIVATE='1')
            m.prepare(env,str(exe));env.update(change)
            p=None
            try:
                p=subprocess.Popen([str(BASE/'build/vxrt'),str(exe)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                time.sleep(.3);self.assertFalse(os.path.exists(m.path))
            finally:
                if p is not None:
                    if p.poll() is None:p.terminate()
                    p.wait(timeout=5)
                m.cleanup()

    def test_private_main_and_single_window(self):
        exe=BASE/'build/cpudiag_guest.exe';m=harness.WindowMetadata(True,cpu=True)
        env=harness.control_environment(dict(os.environ));env.pop('VXRT_PROFILE',None)
        env.update(VXRT_ROOT=str(BASE/'build/cpudiag-root'),VXRT_NO_WINE='1',VXRT_NO_ACTIVATE='1')
        m.prepare(env,str(exe));m.schedule=(.2,2.7,2.8)
        p=None
        try:
            p=subprocess.Popen([str(BASE/'build/vxrt'),str(exe)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            stop=threading.Event();t=threading.Thread(target=m.collect,args=(stop,time.monotonic()));t.start();t.join(8)
            self.assertFalse(t.is_alive());self.assertEqual(len(m.rows),3)
            self.assertTrue(all(row['status']=='ok' for row in m.rows), [row['status'] for row in m.rows])
            self.assertEqual(m.cpu.pid,p.pid)
            self.assertEqual(m.cpu.value['status'],'complete')
            self.assertTrue(any(h['module']=='main_image' for h in m.cpu.value['hotspots']))
            self.assertLess(len(json.dumps(m.cpu.value)),3072)
        finally:
            if p is not None:
                if p.poll() is None:p.terminate()
                p.wait(timeout=5)
            m.cleanup()

if __name__=='__main__':unittest.main()
