#!/usr/bin/env python3
"""Bounded wait transport and evidence tests; no games or guest execution."""
import json
import os
import re
from pathlib import Path
import stat
import sys
import tempfile
import threading
import unittest
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

BASE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BASE.parent / 'tools'))
import vxrt_run as h


def snapshot():
    return dict(status='active', scope='selected_process_including_loaded_dlls',
                openvr_submit='not_instrumented', object_identity='address_or_handle_not_lifetime',
                max_threads=64, max_depth=8, overflow=False, last_present_thread=1,
                threads=[dict(thread=1, entered=1, completed=0, active=True, truncated=False,
                              frames=[dict(api='Sleep', phase='entered', object=0, caller_module='main_image',
                                           caller_offset=123, started_ns=456, result=None)])])


class WaitDiagnostic(unittest.TestCase):
    def test_opt_in_and_environment_scrub(self):
        env = h.control_environment({'VXRT_WAIT_DIAG': '1', 'VXRT_CONTROL': '/tmp/secret', 'KEEP': 'yes'})
        self.assertEqual(env, {'KEEP': 'yes'})
        m = h.WindowMetadata(False)
        m.prepare({'VXRT_WAIT_DIAG': '1'}, '/tmp/game.exe')
        self.assertIsNone(m.path)
        self.assertFalse(m.wait_config['enabled'])
        m = h.WindowMetadata(False, wait=True)
        try:
            m.prepare(env, '/tmp/game.exe')
            self.assertEqual(env['VXRT_WAIT_DIAG'], '1')
            self.assertEqual(env['VXRT_CONTROL_TARGET'], os.path.realpath('/tmp/game.exe'))
            self.assertEqual(env['VXRT_CONTROL_DIAG'], 'render')
            self.assertEqual(m.wait_config['schedule_seconds'], [30, 40, 50])
        finally:
            m.cleanup()
        with self.assertRaises(ValueError):
            h.WindowMetadata(False, wait='1')
        with self.assertRaises(ValueError):
            h.WindowMetadata(False, wait=True).prepare({'VXRT_TRACE_CALLS': '1'}, '/tmp/game.exe')
        with patch.dict(os.environ, VXRT_TRACE_CALLS='5'), patch.object(h, 'resolve') as resolve:
            with self.assertRaises(ValueError):
                h.run('unused', wait_diagnostic=True)
            resolve.assert_not_called()

    def test_settings_conflict_refused_before_build(self):
        spec = {'appid': '1', 'launcher': 'game', 'scenarios': {'start': {}}, 'default_scenario': 'start'}
        with tempfile.TemporaryDirectory() as tmp, patch.object(h, 'REPO', tmp), \
             patch.object(h, 'resolve', return_value=('fixture', spec, {'exe': '/tmp/game.exe'})), \
             patch.object(h, 'git_state', return_value=('test', True)), \
             patch.object(h.doctor, 'machine_info', return_value={}), \
             patch.object(h, 'preconditions', return_value=[]), \
             patch.object(h, 'finish', side_effect=lambda result, *args: result), \
             patch.object(h.subprocess, 'run', side_effect=AssertionError('must not build')):
            result = h.run('fixture', wait_diagnostic=True, settings={'VXRT_TRACE_CALLS': 0})
        self.assertEqual(result['status'], 'refused')
        self.assertIn('wait_diagnostic_compatible', result['refused'])
        self.assertEqual(result['wait_diagnostic']['request_count'], 0)

    def test_hook_labels_have_whitelist_entries(self):
        source = '\n'.join(p.read_text() for p in (BASE / 'src').glob('*') if p.suffix in ('.c', '.m'))
        apis = set(re.findall(r'VX_WAITDIAG_SCOPE\("([^" ]+)"', source))
        phases = set(re.findall(r'vx_waitdiag_phase\("([^" ]+)"', source))
        self.assertFalse(apis - set(h.WAIT_APIS))
        self.assertFalse(phases - set(h.WAIT_PHASES))

    def test_whitelist_projection_and_bounds(self):
        value = snapshot()
        value['secret'] = value['threads'][0]['secret'] = value['threads'][0]['frames'][0]['secret'] = 'SECRET'
        self.assertNotIn('SECRET', json.dumps(h.validate_wait_snapshot(value)))
        for key, bad in [('api', 'SECRET'), ('phase', 'SECRET'), ('caller_module', 'SECRET'),
                         ('object', 257), ('caller_offset', -1), ('started_ns', True), ('result', 2**63)]:
            value = snapshot()
            value['threads'][0]['frames'][0][key] = bad
            self.assertIsNone(h.validate_wait_snapshot(value), key)
        for key, bad in [('thread', 0), ('thread', 65), ('entered', True), ('active', 1), ('frames', [{}] * 9)]:
            value = snapshot()
            value['threads'][0][key] = bad
            self.assertIsNone(h.validate_wait_snapshot(value), key)
        value = snapshot()
        value['threads'] *= 2
        self.assertIsNone(h.validate_wait_snapshot(value))
        value = snapshot()
        value['threads'][0]['active'] = False
        value['threads'][0]['frames'][0].update(result=-2**63, phase='completed')
        self.assertIsNotNone(h.validate_wait_snapshot(value))
        for status in ('off', 'conflict', 'unsupported_arch'):
            value.update(status=status, threads=[])
            self.assertIsNotNone(h.validate_wait_snapshot(value))

    def sample(self, payload, timeout=False, mismatch=False):
        m = h.WindowMetadata(False, wait=True)
        m.path = '/private/tmp/vxclip-test/control.sock'
        m.owns_directory = lambda: True
        m.socket_identity = (1, 2 if not mismatch else 9)
        st = SimpleNamespace(st_mode=stat.S_IFSOCK, st_uid=os.getuid(), st_dev=1, st_ino=2)
        client = MagicMock()
        client.__enter__.return_value = client
        client.recv.side_effect = [TimeoutError()] if timeout else [payload]
        with patch.object(h.os, 'lstat', return_value=st), patch.object(h.socket, 'socket', return_value=client):
            row = m.sample(wait=True)
        return row, client

    def test_protocol_projection_limit_timeout_identity(self):
        raw = dict(schema_version=1, status='ok', pid=123, wait=snapshot(), secret='SECRET')
        row, client = self.sample(json.dumps(raw).encode() + b'\n')
        self.assertEqual(row['status'], 'ok')
        self.assertNotIn('SECRET', json.dumps(row))
        client.sendall.assert_called_once_with(b'wait_snapshot\n')
        self.assertEqual(client.recv.call_args.args, (131072,))
        for payload in (b'x' * 131072, b'', b'{"status":"ok"}\n'):
            row, _ = self.sample(payload)
            self.assertEqual(row['status'], 'invalid_response')
        row, _ = self.sample(b'', timeout=True)
        self.assertEqual(row['status'], 'timeout')
        row, client = self.sample(b'', mismatch=True)
        self.assertEqual(row['status'], 'identity_mismatch')
        client.connect.assert_not_called()

    def test_collect_schedule_combination_and_stop(self):
        for windows, render in ((False, False), (True, False), (False, True), (True, True)):
            m = h.WindowMetadata(windows, render, wait=True)
            m.schedule = (0, 0, 0)
            m.remember_socket = lambda: None
            m.sample = MagicMock(side_effect=lambda **kw: dict(status='ok', pid=123, **({'wait': snapshot()} if kw else {})))
            m.collect(threading.Event(), h.time.monotonic())
            self.assertEqual(len(m.wait_rows), 3)
            self.assertEqual(m.wait_config['request_count'], 3)
            self.assertEqual(m.sample.call_count, 6 if windows or render else 3)
            self.assertTrue(h.valid_wait_samples(m.wait_rows))
        stopped = threading.Event(); stopped.set()
        m = h.WindowMetadata(False, wait=True)
        m.collect(stopped, h.time.monotonic())
        self.assertEqual(m.wait_config['status'], 'stopped')
        self.assertEqual(m.wait_rows, [])

    def test_seal_and_validate_evidence(self):
        m = h.WindowMetadata(False, wait=True)
        m.wait_rows = [dict(status='ok', pid=123, wait=snapshot(), sample_index=1, elapsed_seconds=30)]
        m.wait_config.update(request_count=1, success_count=1)
        with tempfile.TemporaryDirectory() as tmp:
            evidence = h.Evidence()
            # Core may still be under development by another agent; no runtime required.
            real_hash = h.ledger.sha256_file
            with patch.object(h.ledger, 'sha256_file', side_effect=lambda path: real_hash(path) if os.path.exists(path) else '0'*64):
                m.seal(tmp, evidence)
            result = dict(observed=evidence.observed, wait_diagnostic=m.wait_config, game='unused', scenario='start')
            with patch.object(h, 'baseline_for', return_value=(None, None, None)), patch.object(h, 'derive'):
                self.assertEqual(h.validate(result), [])
                result['observed'][0]['value'][0]['wait']['threads'][0]['secret'] = 'SECRET'
                self.assertIn('wait_diagnostic_samples: invalid bounded schema', h.validate(result))
            self.assertEqual(evidence.observed[0]['metric'], 'wait_diagnostic_samples')
            self.assertEqual(set(m.wait_config['source_hashes']), set(h.WAIT_SOURCES))
            self.assertEqual(m.wait_config['sha256'], real_hash(m.wait_config['artifact'] if os.path.isabs(m.wait_config['artifact']) else os.path.join(h.REPO, m.wait_config['artifact'])))


if __name__ == '__main__':
    unittest.main()
