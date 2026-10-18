#!/usr/bin/env python3
"""Validate the opt-in runtime PC diagnostic; never invoke an external profiler."""
import json
import os
import socket
import stat
import time

STATES = frozenset(('off', 'armed', 'running', 'conflict', 'system_error', 'unsupported_arch',
                    'thread_error', 'timer_error', 'window_overrun', 'complete'))

def validate(value):
    if not isinstance(value, dict) or type(value.get('status')) is not str or value['status'] not in STATES:
        return None
    state = value['status']
    if state not in ('complete', 'window_overrun', 'timer_error', 'conflict') or 'hotspots' not in value:
        return {'status': state} if state != 'complete' else None
    fields = ('elapsed_ms', 'samples', 'unknown', 'unpublished', 'other', 'dropped')
    if any(type(value.get(k)) is not int or not 0 <= value[k] <= (100000 if k == 'elapsed_ms' else 2**32-1 if k == 'dropped' else 128) for k in fields):
        return None
    if type(value.get('overflow')) is not bool or not isinstance(value.get('hotspots'), list) or len(value['hotspots']) > 32:
        return None
    if state == 'complete' and not 2000 <= value['elapsed_ms'] <= 2250:
        return None
    rows = []
    seen = set()
    for row in value['hotspots']:
        if not isinstance(row, dict) or row.get('module') not in ('main_image', 'runtime', 'vrclient_x64', 'openvr_api', 'msvcr100', 'msvcp100', 'ucrtbase', 'vcruntime140', 'msvcp140', 'system_kernel', 'system_pthread', 'system_c', 'system_dispatch', 'system_objc'):
            return None
        if type(row.get('offset')) is not int or not 0 <= row['offset'] < 2**64:
            return None
        if type(row.get('weight')) is not int or not 1 <= row['weight'] <= 128:
            return None
        key = (row['module'], row['offset'])
        if key in seen:
            return None
        seen.add(key)
        rows.append({k: row[k] for k in ('module', 'offset', 'weight')})
    if state != 'complete' and rows:
        return None
    if state == 'complete' and sum(r['weight'] for r in rows) + sum(value[k] for k in ('unknown', 'unpublished', 'other')) != value['samples']:
        return None
    return dict(status=state, **{k: value[k] for k in fields}, overflow=value['overflow'], hotspots=rows)

class CpuDiagnostic:
    def __init__(self, enabled):
        if type(enabled) is not bool:
            raise ValueError('cpu_diagnostic must be a boolean')
        self.enabled, self.pid, self.value = enabled, None, {'status': 'not_sampled'}
        self.config = dict(enabled=enabled, mode='pc_only' if enabled else 'off', duration_ms=2000,
                           interval_cpu_ms=20, max_samples=128, max_hotspots=32,
                           window_tolerance_ms=250, signal_handler_retained=True if enabled else False,
                           thread_distribution='process_directed_cpu_timer', stack_capture=False,
                           target_selector='exact_realpath_main_process', child_excluded=True,
                           status='not_started' if enabled else 'off')

    def collect(self, windows, row, index):
        if not self.enabled:
            return
        if index == 1:
            if row.get('status') == 'ok':
                self.pid = row['pid']
                self.config['status'] = 'triggered'
            else:
                self.value = {'status': 'target_unavailable'}
            return
        if index != 2 or self.pid is None:
            return
        if row.get('status') != 'ok' or row.get('pid') != self.pid:
            self.value = {'status': 'identity_mismatch'}
            return
        try:
            if not windows.owns_directory():
                raise ValueError()
            st = os.lstat(windows.path)
            if not stat.S_ISSOCK(st.st_mode) or st.st_uid != os.getuid() or (st.st_dev, st.st_ino) != windows.socket_identity:
                raise ValueError()
            deadline = time.monotonic() + 2
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(2)
                client.connect(windows.path)
                client.sendall(b'cpu\n')
                data = b''
                while b'\n' not in data:
                    client.settimeout(max(.001, deadline - time.monotonic()))
                    chunk = client.recv(4096 - len(data))
                    if not chunk or len(data) + len(chunk) >= 4096 or time.monotonic() >= deadline:
                        raise ValueError()
                    data += chunk
                payload = json.loads(data.split(b'\n', 1)[0])
            if not isinstance(payload, dict) or payload.get('status') != 'ok' or type(payload.get('pid')) is not int or payload['pid'] != self.pid:
                raise ValueError()
            value = validate(payload.get('cpu'))
            if value is None:
                raise ValueError()
            self.value = value
        except (OSError, ValueError, TypeError, OverflowError):
            self.value = {'status': 'invalid_response'}
        self.config['status'] = self.value['status']

    def seal(self, run_dir, evidence):
        if not self.enabled:
            return
        path = os.path.join(run_dir, 'cpu_diagnostic.json')
        with open(path, 'w') as out:
            json.dump(self.value, out, sort_keys=True)
        self.config['status'] = self.value['status']
        evidence.observe('cpu_diagnostic', self.value, path,
                         note='PC sample weights, not all-thread CPU percentages or wait stacks; SIGPROF handler remains inactive until exit')
