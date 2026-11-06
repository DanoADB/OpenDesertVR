# SPDX-License-Identifier: MIT
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

repo = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(repo / "experiments/shader-consumer-audit"))
sys.path.insert(0, str(repo / "experiments/frame-capture"))
import inspect_shader_entry as probe
from test_pamt_metadata import fixture as metadata_fixture
from test_shader_container import fixture as container_fixture


class EntryTests(unittest.TestCase):
    @staticmethod
    def owned(root, flags=0):
        package = root / "0000"
        package.mkdir()
        payload = container_fixture()
        data = bytearray(metadata_fixture())
        struct.pack_into("<II", data, len(data) - 40 + 8, len(payload), len(payload))
        struct.pack_into("<H", data, len(data) - 40 + 18, flags)
        index = package / "0.pamt"
        index.write_bytes(data)
        (package / "0.paz").write_bytes(bytes(100) + payload)
        args = SimpleNamespace(game_root=root, index=index, steam_manifest=root / "owned-manifest",
                               index_sha256=hashlib.sha256(data).hexdigest(),
                               entry_name_sha256=hashlib.sha256(b"shadercache__/d3d12/owned_a.padxil").hexdigest())
        return args, payload

    def test_exact_range_is_read_twice_and_only_metadata_reported(self):
        with tempfile.TemporaryDirectory() as folder:
            args, payload = self.owned(Path(folder))
            report = {"archive_payload_access": "NOT_RUN"}
            with patch("game_build_gate.validate_game_build", return_value={"owned": True}) as gate:
                probe.run(args, report)
            self.assertEqual(gate.call_count, 2)
            self.assertEqual(report["status"], "OBSERVED_SINGLE_SHADER_CONTAINER")
            self.assertEqual(report["archive_bytes_read"], len(payload) * 2)
            self.assertEqual(report["payload_sha256"], hashlib.sha256(payload).hexdigest())
            self.assertEqual(report["input_postflight"], "PASS")
            self.assertNotIn("owned_a", str(report))
            self.assertNotIn(str(args.game_root), str(report))

    def test_bad_selector_fingerprint_and_encoded_flags_reject_before_payload_access(self):
        for change in ("selector", "fingerprint", "encrypted", "compressed"):
            with tempfile.TemporaryDirectory() as folder:
                args, _ = self.owned(Path(folder), flags={"encrypted": 16, "compressed": 2}.get(change, 0))
                if change == "selector":
                    args.entry_name_sha256 = "0" * 64
                if change == "fingerprint":
                    args.index_sha256 = "0" * 64
                report = {"archive_payload_access": "NOT_RUN"}
                with patch("game_build_gate.validate_game_build", return_value={"owned": True}), self.assertRaises(ValueError):
                    probe.run(args, report)
                self.assertEqual(report["archive_payload_access"], "NOT_RUN")

    def test_archive_bounds_and_postflight_changes_cannot_produce_a_pass(self):
        for mode in ("range", "index_change"):
            with tempfile.TemporaryDirectory() as folder:
                args, _ = self.owned(Path(folder))
                report = {}
                if mode == "range":
                    args.index.with_name("0.paz").write_bytes(b"short")
                actual = probe.read_index
                reads = 0

                def changed(path):
                    nonlocal reads
                    reads += 1
                    data, digest = actual(path)
                    return data, "0" * 64 if reads == 2 else digest

                with patch("game_build_gate.validate_game_build", return_value={"owned": True}), patch.object(
                        probe, "read_index", side_effect=changed), self.assertRaises(ValueError):
                    probe.run(args, report)
                self.assertNotEqual(report.get("status"), "OBSERVED_SINGLE_SHADER_CONTAINER")

    def test_cli_failure_is_sanitized_and_does_not_overwrite_existing_evidence(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output = root / "report.json"
            command = [sys.executable, str(repo / "experiments/shader-consumer-audit/inspect_shader_entry.py"),
                       "--game-root", str(root), "--steam-manifest", str(root / "missing"), "--index", str(root / "wrong"),
                       "--index-sha256", "0" * 64, "--entry-name-sha256", "0" * 64, "--output", str(output)]
            first = subprocess.run(command, capture_output=True, timeout=5)
            self.assertEqual(first.returncode, 3)
            saved = output.read_bytes()
            self.assertNotIn(str(root).encode(), saved)
            second = subprocess.run(command, capture_output=True, timeout=5)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(output.read_bytes(), saved)


if __name__ == "__main__":
    unittest.main()
