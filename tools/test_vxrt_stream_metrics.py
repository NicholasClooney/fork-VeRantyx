import unittest

from vxrt_run import encoder_drop_delta


class EncoderDropsTest(unittest.TestCase):
    def test_missing_baseline_is_unknown(self):
        self.assertIsNone(encoder_drop_delta([], 30))
        self.assertIsNone(encoder_drop_delta([7], 30))

    def test_early_exit_uses_available_samples(self):
        self.assertEqual(encoder_drop_delta([0, 0], 30), 0)
        self.assertEqual(encoder_drop_delta([7, 11], 30), 4)
        self.assertEqual(encoder_drop_delta([7, 8, 12], 30), 5)

    def test_full_window_excludes_older_samples(self):
        self.assertEqual(encoder_drop_delta([0, 2, 3, 4, 5, 7, 9, 12], 30), 9)
        self.assertEqual(encoder_drop_delta([1, 4, 8], 10), 4)


if __name__ == "__main__":
    unittest.main()
