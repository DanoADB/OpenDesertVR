# SPDX-License-Identifier: MIT
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments/frame-capture"))
from diagnostic_log import snapshot_log


class DiagnosticLogTests(unittest.TestCase):
    def test_missing_source_does_not_create_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(snapshot_log(root / "missing", root / "snapshot"), {"status": "UNAVAILABLE"})
            self.assertFalse((root / "snapshot").exists())

    def test_snapshot_is_bounded_and_fingerprinted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "source").write_bytes(b"0123456789")
            result = snapshot_log(root / "source", root / "snapshot", 4)
            self.assertEqual((root / "snapshot").read_bytes(), b"0123")
            self.assertEqual(result["snapshot_sha256"], hashlib.sha256(b"0123").hexdigest())
            self.assertEqual(result["initial_source_bytes"], 10)
            self.assertEqual(result["snapshot_bytes"], 4)
            self.assertTrue(result["truncated"])

    def test_empty_log_is_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "source").write_bytes(b"")
            result = snapshot_log(root / "source", root / "snapshot")
            self.assertEqual(result["status"], "SAVED")
            self.assertFalse(result["truncated"])

    def test_existing_snapshot_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "source").write_bytes(b"new")
            (root / "snapshot").write_bytes(b"original")
            with self.assertRaises(FileExistsError):
                snapshot_log(root / "source", root / "snapshot")
            self.assertEqual((root / "snapshot").read_bytes(), b"original")

    def test_source_and_invalid_limit_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source"
            path.write_bytes(b"original")
            with self.assertRaises(ValueError):
                snapshot_log(path, path)
            with self.assertRaises(ValueError):
                snapshot_log(path, path.with_suffix(".snapshot"), 0)
            self.assertEqual(path.read_bytes(), b"original")


if __name__ == "__main__":
    unittest.main()
