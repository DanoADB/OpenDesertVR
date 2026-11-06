# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

script = Path(__file__).resolve().parents[1] / "experiments/shader-consumer-audit/pamt_directory_probe.py"
spec = importlib.util.spec_from_file_location("pamt_directory_probe", script)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def index(block=b"shadercache__\x00d3d12", ids=(0,)):
    return (struct.pack("<III", 0, len(ids), 0) +
            b"".join(struct.pack("<III", value, 0, 0) for value in ids) +
            struct.pack("<I", len(block)) + block)


class DirectoryProbeTests(unittest.TestCase):
    def test_only_selected_directory_span_is_searched(self):
        report = probe.directory_markers(index(b"ordinary") + b"shadercache__d3d12")
        self.assertEqual(report["marker_counts"], {"shadercache__": 0, "d3d12": 0})
        report = probe.directory_markers(index(b"shadercache__\x00d3d12/d3d12"))
        self.assertEqual(report["marker_counts"], {"shadercache__": 1, "d3d12": 2})
        self.assertNotIn("path", report)

    def test_truncated_counts_duplicate_ids_and_oversized_spans_reject(self):
        bad = [b"", index(ids=()), index(ids=(1, 1)), index(ids=(65536,)),
               index()[:-1], struct.pack("<III", 0, 65537, 0) + b"xxxx"]
        oversized = bytearray(index())
        struct.pack_into("<I", oversized, 24, probe.MAX_DIRECTORY + 1)
        bad.append(oversized)
        for data in bad:
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                probe.directory_markers(data)

    def test_file_and_aggregate_limits_apply_before_a_game_read(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                probe.indices(root)
            (root / "0000").mkdir()
            path = root / "0000/0.pamt"
            path.write_bytes(index())
            self.assertEqual(probe.indices(root), [path])
            self.assertEqual(probe.read_index(path)[0], index())
            with path.open("wb") as stream:
                stream.truncate(probe.MAX_TOTAL + 1)
            with self.assertRaises(ValueError):
                probe.indices(root)
            with self.assertRaises(ValueError):
                probe.read_index(path)

    def test_failed_cli_report_is_sanitized_and_existing_evidence_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "report.json"
            command = [sys.executable, str(script), "--game-root", str(root),
                       "--steam-manifest", str(root / "missing"), "--output", str(output)]
            first = subprocess.run(command, capture_output=True, timeout=5)
            self.assertEqual(first.returncode, 3)
            original = output.read_bytes()
            self.assertNotIn(str(root).encode(), original)
            second = subprocess.run(command, capture_output=True, timeout=5)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(output.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
