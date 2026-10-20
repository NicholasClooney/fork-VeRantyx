#!/usr/bin/env python3
"""Synthetic PNGs and mocked transport only; never load or analyze game images."""
import copy
import json
import os
from pathlib import Path
import socket
import struct
import tempfile
import threading
import unittest
from unittest.mock import Mock, patch
import zlib
import vxrt_run as h


def synthetic_png(width=2, height=1):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(b'\0' + b'\xff\0\0\xff' * width)) + chunk(b'IEND', b''))


class Snapshot(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = os.path.realpath(self.tmp.name)
        self.run_dir = os.path.join(self.repo, 'runs', 'synthetic')
        os.makedirs(self.run_dir)
        for source in h.PRESENT_SOURCES:
            path = Path(self.repo, source)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('synthetic source\n')
        self.repo_patch = patch.object(h, 'REPO', self.repo)
        self.repo_patch.start()
        self.addCleanup(self.repo_patch.stop)
        self.addCleanup(self.tmp.cleanup)

    def prepared(self):
        snapshot = h.PresentSnapshot(60, 90)
        snapshot.prepare({}, self.run_dir)
        return snapshot

    def sealed(self, status='armed', early=False, image=True):
        snapshot = self.prepared()
        transport = Mock()
        transport.sample.side_effect = [{'status': 'ok', 'pid': 431}, {'status': status, 'pid': 431}]
        snapshot.arm(transport, 60)
        if image:
            Path(self.run_dir, 'present_snapshot.png').write_bytes(synthetic_png())
        evidence = h.Evidence()
        evidence.observe('old_observation', 0)
        evidence.observe('exited_early', early)
        snapshot.seal(self.run_dir, evidence, early)
        result = {'run_id': 'synthetic', 'seconds': 90, 'present_snapshot': snapshot.config,
                  'observed': evidence.observed, 'status': 'no_baseline'}
        return snapshot, evidence, result

    def test_bounds(self):
        for at in (True, 0, -1, 29.9, 3601, float('inf'), float('nan'), '60', 90):
            with self.subTest(at=at), self.assertRaises(ValueError):
                h.PresentSnapshot(at, 90)
        h.PresentSnapshot(30, 31)
        h.PresentSnapshot(3600, 3601)

    def test_off_clears_all_inherited_switches(self):
        env = {'VXRT_PRESENT_SNAPSHOT': 'secret', 'VXRT_PRESENT_SNAPSHOT_AT': '1',
               'VXRT_PRESENT_SNAPSHOT_CONTROL': '1', 'VXRT_PRESENT_SNAPSHOT_FUTURE': '1', 'OTHER': 'yes'}
        self.assertEqual(h.control_environment(env), {'OTHER': 'yes'})
        snapshot = h.PresentSnapshot(None, 90)
        snapshot.prepare(env, self.run_dir)
        endpoint = Mock()
        snapshot.arm(endpoint, 60)
        snapshot.seal(self.run_dir, h.Evidence(), False)
        endpoint.sample.assert_not_called()
        self.assertEqual(env, {'OTHER': 'yes'})
        self.assertEqual(os.listdir(self.run_dir), [])
        self.assertEqual(h.validate_present_snapshot({'seconds': 90, 'present_snapshot': snapshot.config}), [])
        for key in ('VXRT_PRESENT_SNAPSHOT', 'VXRT_PRESENT_SNAPSHOT_CONTROL', 'VXRT_PRESENT_SNAPSHOT_AT'):
            self.assertNotIn(key, h.TUNABLES)
            self.assertNotIn(key, h.SAFETY_ENV)

    def test_exact_one_handshake_and_arm_uses_real_pid(self):
        snapshot = self.prepared()
        endpoint = Mock()
        endpoint.sample.side_effect = [{'status': 'ok', 'pid': 9876}, {'status': 'armed', 'pid': 9876}]
        snapshot.arm(endpoint, 60)
        snapshot.arm(endpoint, 70)
        self.assertEqual(endpoint.sample.call_count, 2)
        endpoint.sample.assert_called_with(snapshot_pid=9876)
        self.assertEqual(snapshot.config['request_count'], 1)
        self.assertEqual(snapshot.config['handshake_count'], 1)

    def test_no_pid_never_arms(self):
        for response in ({'status': 'unavailable'}, {'status': 'ok', 'pid': True}, {'status': 'ok', 'pid': 0}):
            snapshot = self.prepared()
            endpoint = Mock()
            endpoint.sample.return_value = response
            snapshot.arm(endpoint, 60)
            endpoint.sample.assert_called_once_with()
            self.assertEqual(snapshot.config['status'], 'no_pid')
            self.assertEqual(snapshot.config['request_count'], 0)

    def test_refused_run_never_prepares_or_arms(self):
        spec = {'appid': '1', 'launcher': 'game', 'scenarios': {'start': {}}, 'default_scenario': 'start'}
        with patch.object(h, 'resolve', return_value=('fixture', spec, {'exe': '/synthetic/main.exe'})), \
             patch.object(h, 'git_state', return_value=('test', True)), \
             patch.object(h.doctor, 'machine_info', return_value={}), \
             patch.object(h, 'preconditions', return_value=[{'code': 'fixture_refused', 'ok': False}]), \
             patch.object(h, 'finish', side_effect=lambda result, *args: result), \
             patch.object(h.PresentSnapshot, 'prepare', side_effect=AssertionError('no preparation')):
            result = h.run('fixture', seconds=90, present_snapshot_at=60)
        self.assertEqual(result['status'], 'refused')
        self.assertEqual(result['present_snapshot']['request_count'], 0)
        self.assertEqual(h.validate_present_snapshot(result), [])

    def test_socket_identity_change_never_sends(self):
        windows = h.WindowMetadata(False)
        windows.enabled = True
        windows.prepare({}, '/synthetic/main.exe')
        self.addCleanup(windows.cleanup)
        with patch.object(windows, 'owns_directory', return_value=False), \
             patch.object(h.socket, 'socket', side_effect=AssertionError('must not connect')):
            self.assertEqual(windows.sample(snapshot_pid=42), {'status': 'identity_mismatch'})

    def test_header_and_opaque_hash_only(self):
        image = synthetic_png()
        path = Path(self.run_dir, 'present_snapshot.png')
        path.write_bytes(image)
        with patch.object(h.zlib, 'decompress', side_effect=AssertionError('no pixel decoding')):
            metadata = h.present_png_metadata(self.run_dir)
        self.assertEqual((metadata['width'], metadata['height'], metadata['format']), (2, 1, 'PNG'))
        self.assertEqual(metadata['size_bytes'], len(image))
        self.assertEqual(metadata['sha256'], h.hashlib.sha256(image).hexdigest())

    def test_file_guards(self):
        path = Path(self.run_dir, 'present_snapshot.png')
        target = Path(self.repo, 'synthetic.png')
        target.write_bytes(synthetic_png())
        path.symlink_to(target)
        with self.assertRaises((OSError, ValueError)):
            h.present_png_metadata(self.run_dir)
        with self.assertRaises(ValueError):
            self.prepared()
        path.unlink()
        os.link(target, path)
        with self.assertRaises(ValueError):
            h.present_png_metadata(self.run_dir)
        path.unlink()
        path.mkdir()
        with self.assertRaises((OSError, ValueError)):
            h.present_png_metadata(self.run_dir)
        path.rmdir()
        os.mkfifo(path)
        with self.assertRaises(ValueError):
            h.present_png_metadata(self.run_dir)
        path.unlink()
        path.write_bytes(synthetic_png())
        with patch.object(h, 'PRESENT_MAX_BYTES', 44), self.assertRaises(ValueError):
            h.present_png_metadata(self.run_dir)
        path.write_bytes(b'not a PNG' * 10)
        with self.assertRaises(ValueError):
            h.present_png_metadata(self.run_dir)
        path.write_bytes(synthetic_png(width=16385))
        with self.assertRaises(ValueError):
            h.present_png_metadata(self.run_dir)

    def test_append_and_validate_captured(self):
        snapshot, evidence, result = self.sealed()
        self.assertEqual(snapshot.config['status'], 'captured')
        self.assertEqual([o['id'] for o in evidence.observed], ['o1', 'o2', 'o3', 'o4'])
        self.assertEqual(h.validate_present_snapshot(result), [])
        for key, value in [('request_count', 2), ('handshake_count', 0), ('pid', 999), ('pixel_analysis', True),
                           ('at_seconds', 90), ('source_hashes', {}), ('status', 'no_png')]:
            changed = copy.deepcopy(result)
            changed['present_snapshot'][key] = value
            self.assertTrue(h.validate_present_snapshot(changed), key)
        path = Path(self.run_dir, 'present_snapshot.png')
        path.write_bytes(synthetic_png(width=3))
        self.assertTrue(h.validate_present_snapshot(result))

    def test_validation_checks_invariants_even_with_matching_record(self):
        _, _, result = self.sealed(image=False)
        for change in ({'request_count': 2}, {'pid': 999}, {'status': 'early_exit'},
                       {'status': 'captured'}, {'elapsed_seconds': 59}, {'source_hashes': {}},
                       {'arm_response': {'status': 'armed', 'pid': True}}, {'status': 'not_started'}):
            changed = copy.deepcopy(result)
            changed['present_snapshot'].update(change)
            record = next(o for o in changed['observed'] if o['metric'] == 'present_snapshot')
            record['value'] = copy.deepcopy(changed['present_snapshot'])
            Path(self.run_dir, 'present_snapshot.json').write_text(json.dumps(changed['present_snapshot']))
            self.assertTrue(h.validate_present_snapshot(changed), change)

    def test_armed_without_png_and_early_exit(self):
        snapshot, _, result = self.sealed(image=False)
        self.assertEqual(snapshot.config['status'], 'no_png')
        self.assertEqual(h.validate_present_snapshot(result), [])
        Path(self.run_dir, 'present_snapshot.json').unlink()
        snapshot, _, result = self.sealed(image=False, early=True)
        self.assertEqual(snapshot.config['status'], 'early_exit')
        self.assertEqual(h.validate_present_snapshot(result), [])

    def test_early_exit_before_handshake(self):
        snapshot = self.prepared()
        evidence = h.Evidence()
        evidence.observe('exited_early', True)
        snapshot.seal(self.run_dir, evidence, True)
        self.assertEqual(snapshot.config['status'], 'early_exit')
        self.assertEqual(snapshot.config['request_count'], 0)
        result = {'run_id': 'synthetic', 'seconds': 90, 'present_snapshot': snapshot.config, 'observed': evidence.observed}
        self.assertEqual(h.validate_present_snapshot(result), [])

    def test_rejection_does_not_claim_existing_png(self):
        snapshot, _, result = self.sealed(status='disabled')
        self.assertEqual(snapshot.config['status'], 'disabled')
        self.assertNotIn('png', snapshot.config)
        self.assertEqual(h.validate_present_snapshot(result), [])

    def test_invalid_header_is_failure(self):
        snapshot = self.prepared()
        endpoint = Mock()
        endpoint.sample.side_effect = [{'status': 'ok', 'pid': 8}, {'status': 'armed', 'pid': 8}]
        snapshot.arm(endpoint, 60)
        Path(self.run_dir, 'present_snapshot.png').write_bytes(b'bad')
        snapshot.seal(self.run_dir, h.Evidence(), False)
        self.assertEqual(snapshot.config['status'], 'invalid_png')

    def test_schedule_adds_one_handshake_without_changing_old_samples(self):
        windows = h.WindowMetadata(True)
        windows.present = h.PresentSnapshot(60, 90)
        windows.remember_socket = Mock()
        windows.sample = Mock(side_effect=[{'status': 'ok', 'pid': 7}] * 4 + [{'status': 'armed', 'pid': 7}])
        class Clock:
            now = 0
            def is_set(self): return False
            def wait(self, duration): self.now += duration
        clock = Clock()
        with patch.object(h.time, 'monotonic', side_effect=lambda: clock.now):
            windows.collect(clock, 0)
        self.assertEqual(len(windows.rows), 3)
        self.assertEqual(windows.config['request_count'], 3)
        self.assertEqual(windows.sample.call_count, 5)
        self.assertEqual(windows.present.config['request_count'], 1)

    def test_snapshot_only_handshake_seal_and_validate(self):
        windows = h.WindowMetadata(False)
        windows.present = self.prepared()
        windows.enabled = windows.enabled or windows.present.enabled
        env = {}
        windows.prepare(env, '/synthetic/main.exe')
        self.addCleanup(windows.cleanup)
        self.assertEqual(env['VXRT_CONTROL_DIAG'], 'metadata')
        self.assertFalse(windows.window_enabled)
        self.assertFalse(windows.render_enabled)
        self.assertFalse(windows.wait_enabled)
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.addCleanup(server.close)
        server.bind(windows.path)
        server.listen(1)
        server.settimeout(3)
        commands = []
        errors = []
        def serve():
            try:
                # Deliberately omit window/render counters: this handshake needs only the PID.
                for status in ('ok', 'armed'):
                    client, _ = server.accept()
                    with client:
                        commands.append(client.recv(256))
                        client.sendall(json.dumps({'schema_version': 1, 'status': status, 'pid': 431}).encode() + b'\n')
            except Exception as error:
                errors.append(error)
        thread = threading.Thread(target=serve)
        thread.start()
        class Clock:
            now = 0
            def is_set(self): return False
            def wait(self, duration): self.now += duration
        clock = Clock()
        with patch.object(h.time, 'monotonic', side_effect=lambda: clock.now):
            windows.collect(clock, 0)
        thread.join(4)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(commands, [b'metadata\n', b'present_snapshot 431\n'])
        self.assertEqual(windows.rows, [])
        self.assertEqual(windows.wait_rows, [])
        self.assertEqual(windows.present.config['status'], 'armed')
        Path(self.run_dir, 'present_snapshot.png').write_bytes(synthetic_png())
        evidence = h.Evidence()
        evidence.observe('exited_early', False)
        windows.seal(self.run_dir, evidence)
        windows.present.seal(self.run_dir, evidence, False)
        result = {'run_id': 'synthetic', 'seconds': 90, 'present_snapshot': windows.present.config,
                  'observed': evidence.observed, 'status': 'no_baseline', 'game': 'fixture', 'scenario': 'start'}
        self.assertEqual(windows.present.config['status'], 'captured')
        self.assertEqual([o['metric'] for o in evidence.observed],
                         ['exited_early', 'present_snapshot', 'present_snapshot_png'])
        with patch.object(h, 'baseline_for', return_value=(None, None, None)), patch.object(h, 'derive'):
            self.assertEqual(h.validate(result), [])

    def test_transport_pid_and_response_filtering(self):
        windows = h.WindowMetadata(False)
        windows.enabled = True
        windows.prepare({}, '/synthetic/main.exe')
        self.addCleanup(windows.cleanup)
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.addCleanup(server.close)
        server.bind(windows.path)
        server.listen(1)
        windows.remember_socket()
        responses = [{'schema_version': 1, 'status': 'armed', 'pid': 42, 'SECRET': 'do not retain'},
                     {'schema_version': 1, 'status': 'armed', 'pid': 99},
                     {'schema_version': 1, 'status': 'evil payload', 'pid': 42}]
        commands = []
        def serve():
            for response in responses:
                client, _ = server.accept()
                with client:
                    commands.append(client.recv(256))
                    client.sendall(json.dumps(response).encode() + b'\n')
        thread = threading.Thread(target=serve)
        thread.start()
        self.assertEqual(windows.sample(snapshot_pid=42), {'status': 'armed', 'pid': 42})
        self.assertEqual(windows.sample(snapshot_pid=42), {'status': 'wrong_pid', 'pid': 99})
        self.assertEqual(windows.sample(snapshot_pid=42), {'status': 'invalid_response'})
        thread.join(3)
        self.assertFalse(thread.is_alive())
        self.assertEqual(commands, [b'present_snapshot 42\n'] * 3)

    def test_compositor_archive_is_raw_and_appended(self):
        overlay = Path(self.repo, 'overlay')
        (overlay / 'logs').mkdir(parents=True)
        raw = b'historical Submit=100\x00\xff\n'
        (overlay / 'logs/vrcompositor.txt').write_bytes(raw)
        logs = Path(self.run_dir, 'logs')
        logs.mkdir()
        evidence = h.Evidence()
        evidence.observe('old', 1)
        h.archive_compositor(str(overlay), str(logs), evidence)
        self.assertEqual((logs / 'vrcompositor.txt').read_bytes(), raw)
        self.assertEqual(evidence.observed[-1]['id'], 'o2')
        self.assertEqual(evidence.observed[-1]['metric'], 'vrcompositor_log_archive')
        self.assertEqual(evidence.derived, [])
        self.assertEqual(evidence.observed[-1]['sha256'], h.hashlib.sha256(raw).hexdigest())


    def test_trace_sealing_validation_and_tampering(self):
        snapshot, evidence, result = self.sealed(image=False)
        logs = Path(self.run_dir, "logs")
        logs.mkdir()
        log = logs / "vxrt-synthetic.log"
        log.write_text(CaptureTrace.line("claimed") + CaptureTrace.line("done", "unsupported_format"))
        evidence.observe("present_snapshot_trace", h.present_snapshot_trace(str(log), 431), str(log))
        self.assertEqual(h.validate_present_snapshot(result), [])
        result["observed"][-1]["value"]["records"][-1]["category"] = "ok"
        self.assertTrue(h.validate_present_snapshot(result))
        result["observed"][-1]["value"] = {"status": "unavailable", "pid": 431, "records": []}
        result["observed"][-1].pop("artifact")
        result["observed"][-1].pop("sha256")
        self.assertEqual(h.validate_present_snapshot(result), [])
        del result["present_snapshot"]
        self.assertTrue(h.validate_present_snapshot(result))


class CaptureTrace(unittest.TestCase):
    @staticmethod
    def line(stage, category="none", pid=431, width=64):
        return (f"[vxrt] 10:20:30.000 present_snapshot: pid={pid} stage={stage} "
                f"format=70 width={width} height=48 category={category}\n")

    def parse(self, text, pid=431):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, "synthetic.log")
            path.write_text(text)
            return h.present_snapshot_trace(str(path), pid)

    def test_complete_fixed_metadata(self):
        text = "".join(self.line(stage, "ok" if stage == "done" else "none") for stage in h.PRESENT_STAGES)
        result = self.parse(text)
        self.assertEqual(result["status"], "recorded")
        self.assertEqual(len(result["records"]), 5)
        self.assertEqual(result["records"][-1], {
            "line": 5, "stage": "done", "category": "ok", "format": 70, "width": 64, "height": 48})

    def test_fixed_failures_and_incomplete_trace(self):
        for category in h.PRESENT_CATEGORIES[2:]:
            with self.subTest(category=category):
                result = self.parse(self.line("claimed") + self.line("done", category))
                self.assertEqual(result["status"], "recorded")
                self.assertEqual(result["records"][-1]["category"], category)
        result = self.parse(self.line("claimed") + self.line("readback"))
        self.assertEqual(result["status"], "recorded")
        self.assertEqual(result["records"][-1]["stage"], "readback")
        self.assertEqual(self.parse("")["status"], "not_recorded")
        self.assertEqual(self.parse("", None)["status"], "no_pid")
        self.assertEqual(h.present_snapshot_trace("/nonexistent/synthetic.log", 431)["status"], "unavailable")

    def test_helpers_other_pids_and_payload_noise_excluded(self):
        text = (self.line("claimed", pid=432)
                + self.line("claimed").replace("[vxrt]", "[vxrt helper.exe:431]")
                + self.line("claimed").replace("[vxrt] ", "")
                + self.line("claimed").rstrip() + " arbitrary payload\n")
        self.assertEqual(self.parse(text), {"status": "not_recorded", "pid": 431, "records": []})

    def test_invalid_sequence_dimensions_and_categories(self):
        for text in (self.line("write"), self.line("claimed") * 6,
                     self.line("claimed") + self.line("done", "ok"),
                     self.line("claimed") + self.line("readback", width=65),
                     self.line("claimed", "ok"), self.line("unknown"),
                     self.line("claimed") + self.line("done", "arbitrary"),
                     self.line("claimed", width=4294967296)):
            with self.subTest(text=text):
                self.assertEqual(self.parse(text)["status"], "invalid")


if __name__ == '__main__':
    unittest.main()
