"""Offline regression checks for explicit installs and keyboard diagnostics; never launches a game."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import vxrt_games as games
import vxrt_run as harness
import vxrt_studio as studio


class ProfileHarnessTests(unittest.TestCase):
    def setUp(self):
        self.profile = games.by_key("359320")

    def test_profile_is_keyboard_diagnostic(self):
        self.assertEqual(games.problems(self.profile), [])
        self.assertEqual(self.profile["baselines"], {})
        scenario = self.profile["scenarios"][self.profile["default_scenario"]]
        self.assertEqual(scenario["input"], "keyboard_mouse")
        self.assertEqual(scenario["args"], [])

    def test_explicit_executable_has_no_guessed_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            inst = {"359320": {"folder": tmp, "exe": tmp + "/EDLaunch.exe"}}
            with patch.object(harness, "installed", return_value=inst):
                self.assertIsNone(harness.resolve("359320")[2])
                product = Path(tmp, self.profile["exe"])
                product.parent.mkdir(parents=True)
                product.touch()
                self.assertEqual(harness.resolve("elite")[2]["exe"], str(product.resolve()))

    def test_escape_and_symlink_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as outside:
            exe = Path(outside, "game.exe")
            exe.touch()
            Path(tmp, "escape.exe").symlink_to(exe)
            for path in ("../game.exe", str(exe), "escape.exe"):
                with self.assertRaises(ValueError):
                    games.executable({"exe": path}, tmp)

    def test_local_library_is_explicit_and_no_wine_vdf_is_read(self):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "steamapps").mkdir()
            with patch.dict(os.environ, {"VXRT_STEAM_LIBRARY": tmp, "VXRT_NO_WINE": "1"}), \
                 patch.object(studio, "STEAM_ROOT", "/unused-wine"), \
                 patch("builtins.open", side_effect=AssertionError("must not read Wine VDF")):
                self.assertEqual(studio.libraries()[0], os.path.realpath(tmp))

    def test_safety_environment_cannot_relax_restrictions(self):
        self.assertEqual(harness.safety_environment(harness.SAFETY_ENV), harness.SAFETY_ENV)
        for settings in ({"VERANTYX_LOOPBACK_ONLY": "0"}, {"VERANTYX_AUDIO": "1"},
                         {"VERANTYX_ALVR": "1"}, {"VXRT_MUTE": "0"}, {"TOKEN": "secret"}):
            with self.assertRaises(ValueError):
                harness.safety_environment(settings)

    def test_keyboard_pose_clears_controller_input(self):
        import struct
        class OneFrame:
            stopped = False
            def is_set(self): return self.stopped
            def wait(self, seconds): self.stopped = True
        with tempfile.TemporaryDirectory() as tmp:
            shared = Path(tmp, "shared")
            # Small fake shared layout avoids allocating the real 268 MB frame slots.
            with patch.object(harness, "HANDS", 64):
                shared.write_bytes(bytes([255]) * 364)
                with shared.open("r+b") as f:
                    f.write(bytes(24))
                harness.feed_pose(str(shared), OneFrame(), 15, controllers=False)
                data = shared.read_bytes()
                self.assertEqual(data[64 + 192:64 + 244], bytes(52))
                self.assertEqual(data[64 + 286], 1)
                self.assertEqual(struct.unpack_from("<i", data, 20)[0] % 2, 0)


if __name__ == "__main__":
    unittest.main()
