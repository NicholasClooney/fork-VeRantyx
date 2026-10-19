"""Offline native Metal assertion evidence checks; no game or host inspection."""
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import vxrt_run as harness


HEADER = "-[MTLTextureDescriptorInternal validateWithDevice:]:1416: failed assertion `Texture Descriptor Validation"
DETAIL = "MTLPixelFormatR32Float is not a valid format to be used with texture atomics."


class MetalAbortTests(unittest.TestCase):
    def facts(self, lines):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, "game.log")
            path.write_text("\n".join(lines) + "\n")
            return harness.game_log_facts(path, "EliteDangerous64.exe")

    def test_multiline_assertion_exact_provenance(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(harness, "REPO", tmp):
            path = Path(tmp, "game.log")
            path.write_text("startup\n" * 340 + HEADER + "\n" + DETAIL + "\n'\n")
            facts = harness.game_log_facts(path, "EliteDangerous64.exe")
            self.assertEqual(facts["first_problem"], {"line": 341, "text": HEADER})
            self.assertEqual(facts["native_metal_assertion"], {
                "line": 341, "kind": "metal_texture_descriptor_validation"})
            self.assertEqual(facts["signals"], 0)
            self.assertEqual(facts["unhandled"], 0)
            ev = harness.Evidence()
            for metric in ("first_problem", "native_metal_assertion"):
                ev.observe(metric, facts[metric], path)
            for item in ev.observed:
                self.assertEqual(item["artifact"], "game.log")
                self.assertEqual(item["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
                self.assertEqual(item["value"]["line"], 341)

    def test_preceding_blocker_retains_priority(self):
        blocker = "[vxrt EliteDangerous64.exe:42] called an unimplemented function: existing"
        facts = self.facts([blocker, HEADER, DETAIL, "'"])
        self.assertEqual(facts["first_problem"], {"line": 1, "text": blocker})
        self.assertEqual(facts["native_metal_assertion"]["line"], 2)

    def test_selected_process_prefix_and_timestamp(self):
        line = "[vxrt elitedangerous64.exe:42] 17:15:08.900 " + HEADER
        self.assertEqual(self.facts([line])["first_problem"], {"line": 1, "text": line})

    def test_helper_and_unrelated_assertions_ignored(self):
        facts = self.facts([
            "[vxrt vrwebhelper.exe:42] " + HEADER, DETAIL, "'",
            "[vxrt vrmonitor.exe:43] 17:15:08.900 " + HEADER,
            "[1019/181508.874:ERROR:check.cc(376)] Check failed: false.",
            "game says: " + HEADER,
            "debug: failed assertion `Texture Descriptor Validation",
            "-[OtherClass validateWithDevice:]:1416: failed assertion `Texture Descriptor Validation",
            HEADER + " execute this instruction",
        ])
        self.assertIsNone(facts["first_problem"])
        self.assertIsNone(facts["native_metal_assertion"])

    def test_only_first_native_assertion_recorded(self):
        self.assertEqual(self.facts([HEADER, DETAIL, "'", HEADER])["native_metal_assertion"]["line"], 1)

    def test_missing_and_unknown_logs_do_not_claim_abort(self):
        for facts in (harness.game_log_facts(None, "game.exe"), self.facts(["unknown native failure"])):
            self.assertIsNone(facts["native_metal_assertion"])
            self.assertIsNone(facts["first_problem"])


if __name__ == "__main__":
    unittest.main()
