"""Offline network diagnostic checks; all launch/build/host inspection is mocked."""
from contextlib import ExitStack
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import vxrt_run as harness


NETWORK_BOX = "MessageBox: Network error: Unable to initialise network. Please check your connection."


class NetworkDiagnosticTests(unittest.TestCase):
    def fake_run(self, tmp, **kwargs):
        spec = {"appid": "1", "launcher": "game", "default_scenario": "start",
                "scenarios": {"start": {"seconds": 1}}}
        with ExitStack() as stack:
            stack.enter_context(patch.object(harness, "REPO", tmp))
            stack.enter_context(patch.object(harness, "resolve", return_value=("fake", spec, {"exe": "/fake/game.exe"})))
            stack.enter_context(patch.object(harness, "git_state", return_value=("test", False)))
            stack.enter_context(patch.object(harness.doctor, "machine_info", return_value={}))
            stack.enter_context(patch.object(harness, "preconditions", return_value=[]))
            stack.enter_context(patch.object(harness, "finish", side_effect=lambda result, *args: result))
            stack.enter_context(patch.object(harness.subprocess, "run", return_value=Mock(returncode=0)))
            launch = stack.enter_context(patch.object(harness.subprocess, "Popen", side_effect=RuntimeError("offline launch boundary")))
            stack.enter_context(patch.object(harness.threading, "Thread"))
            try:
                result = harness.run("fake", quiet=True, **kwargs)
            except RuntimeError as error:
                self.assertEqual(str(error), "offline launch boundary")
                return launch.call_args.kwargs["env"]
            launch.assert_not_called()
            return result

    def test_explicit_enable_and_inherited_value_removed_by_default(self):
        for inherited in ("1", "arbitrary"):
            for enabled in (False, True):
                with self.subTest(inherited=inherited, enabled=enabled), tempfile.TemporaryDirectory() as tmp, \
                     patch.dict(os.environ, {"VXRT_NET_DIAG": inherited}, clear=True):
                    env = self.fake_run(tmp, network_diagnostic=enabled)
                    self.assertEqual(env.get("VXRT_NET_DIAG"), "1" if enabled else None)
                    self.assertNotIn("VXRT_TRACE", env)
                    self.assertNotIn("VXRT_TRACE_CALLS", env)
                    self.assertEqual(os.environ["VXRT_NET_DIAG"], inherited)

    def test_fixed_configuration_and_settings_refusal(self):
        for enabled in (False, True):
            with tempfile.TemporaryDirectory() as tmp:
                result = self.fake_run(tmp, network_diagnostic=enabled, settings={"VXRT_NET_DIAG": "1"})
                self.assertEqual(result["status"], "refused")
                self.assertEqual(result["refused"], ["settings_allowed"])
                self.assertEqual(result["network_diagnostic"], {
                    "enabled": enabled, "environment": {"VXRT_NET_DIAG": "1"} if enabled else {},
                    "max_records_per_process": 128 if enabled else 0})
        with self.assertRaises(ValueError):
            harness.safety_environment({"VXRT_NET_DIAG": "1"})

    def test_reject_non_boolean_before_host_inspection(self):
        with patch.object(harness, "resolve", side_effect=AssertionError("must validate first")):
            for value in (None, "1", "0", "true", 1, 0, {}, {"enabled": True}):
                with self.subTest(value=value), self.assertRaisesRegex(ValueError, "must be a boolean"):
                    harness.run("fake", network_diagnostic=value)

    def test_cli_fixed_flag_and_default(self):
        for flags, expected in (([], False), (["--network-diagnostic"], True)):
            with patch("sys.argv", ["vxrt_run.py", "fake"] + flags), \
                 patch.object(harness, "run", return_value={"status": "refused"}) as run, \
                 self.assertRaises(SystemExit) as error:
                harness.main()
            self.assertEqual(error.exception.code, 3)
            self.assertIs(run.call_args.kwargs["network_diagnostic"], expected)
        with patch("sys.argv", ["vxrt_run.py", "fake", "--network-diagnostic=anything"]), \
             patch("sys.stderr", new_callable=io.StringIO), \
             patch.object(harness, "run") as run, self.assertRaises(SystemExit) as error:
            harness.main()
        self.assertEqual(error.exception.code, 2)
        run.assert_not_called()

    def facts(self, lines):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, "game.log")
            path.write_text("\n".join(lines) + "\n")
            return harness.game_log_facts(path, "game.exe")

    def test_network_box_keeps_actual_line_and_text(self):
        line = "[vxrt game.exe:42] 15:40:54.041 " + NETWORK_BOX
        facts = self.facts(["startup", line])
        self.assertEqual(facts["first_problem"], {"line": 2, "text": line})
        self.assertEqual(facts["unhandled"], 0)
        self.assertEqual(facts["unimplemented"], {})

    def test_plain_runtime_network_box(self):
        line = "15:40:54.041 " + NETWORK_BOX
        self.assertEqual(self.facts([line])["first_problem"], {"line": 1, "text": line})

    def test_ordinary_boxes_and_helper_noise_are_ignored(self):
        self.assertIsNone(self.facts([
            "MessageBox: Welcome: Ready to start.",
            "MessageBox: Network error: Something else.",
            "[vxrt vrmonitor.exe:42] " + NETWORK_BOX,
            "[vxrt vrwebhelper.exe:43] " + NETWORK_BOX,
        ])["first_problem"])

    def test_existing_first_problem_retains_priority(self):
        line = "[vxrt game.exe:42] called an unimplemented function: existing"
        facts = self.facts([line, NETWORK_BOX])
        self.assertEqual(facts["first_problem"], {"line": 1, "text": line})
        self.assertEqual(facts["unimplemented"], {"existing": 1})


if __name__ == "__main__":
    unittest.main()
