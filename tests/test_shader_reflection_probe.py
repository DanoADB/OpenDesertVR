# SPDX-License-Identifier: MIT
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

repo = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(repo / "experiments/shader-consumer-audit"))
import query_shader_reflection as probe
from reflection_query import ReflectionError, digest
from shader_container import parse_pasc
from test_reflection_query import valid
import test_shader_entry as entry_fixtures


class ReflectionProbeTests(unittest.TestCase):
    def owned(self, root):
        args, payload = entry_fixtures.EntryTests.owned(root)
        container, _ = parse_pasc(payload)
        args.helper, args.sdk_library = root / "odv_shader_reflection.exe", root / "dxcompiler.dll"
        args.helper.write_bytes(b"owned not executed")
        args.sdk_library.write_bytes(b"owned not loaded")
        args.helper_sha256 = digest(args.helper, 1000)
        args.sdk_library_sha256 = digest(args.sdk_library, 1000)
        args.expected_payload_sha256 = hashlib.sha256(payload).hexdigest()
        args.expected_container_sha256 = hashlib.sha256(container).hexdigest()
        return args, container

    def test_same_owned_container_reaches_helper_after_input_gates(self):
        with tempfile.TemporaryDirectory() as folder:
            args, container = self.owned(Path(folder))
            report = {}
            with patch("game_build_gate.validate_game_build", return_value={"owned": True}), patch.object(
                    probe, "reflect", return_value=valid()) as helper:
                probe.run(args, report)
            helper.assert_called_once_with(container, args.helper, args.helper_sha256, args.sdk_library, args.sdk_library_sha256)
            self.assertEqual(report["status"], "OBSERVED_SHADER_REFLECTION_METADATA")
            self.assertNotIn("owned_a", json.dumps(report))
            self.assertNotIn(str(args.game_root), json.dumps(report))

    def test_changed_expected_payload_or_container_never_reaches_helper(self):
        for field in ("expected_payload_sha256", "expected_container_sha256"):
            with tempfile.TemporaryDirectory() as folder:
                args, _ = self.owned(Path(folder))
                setattr(args, field, "0" * 64)
                report = {}
                with patch("game_build_gate.validate_game_build", return_value={"owned": True}), patch.object(
                        probe, "reflect") as helper, self.assertRaises(ReflectionError):
                    probe.run(args, report)
                helper.assert_not_called()
                self.assertEqual(report["status"], "FAILED")

    def test_cli_rejects_missing_tools_with_sanitized_exclusive_evidence(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output = root / "report.json"
            command = [sys.executable, str(repo / "experiments/shader-consumer-audit/query_shader_reflection.py")]
            for name in ("game-root", "steam-manifest", "index", "helper", "sdk-library"):
                command += ["--" + name, str(root / "missing")]
            for name in ("index-sha256", "entry-name-sha256", "expected-payload-sha256", "expected-container-sha256",
                         "helper-sha256", "sdk-library-sha256"):
                command += ["--" + name, "0" * 64]
            command += ["--output", str(output)]
            first = subprocess.run(command, capture_output=True, timeout=5)
            self.assertEqual(first.returncode, 3)
            saved = output.read_bytes()
            self.assertNotIn(str(root).encode(), saved)
            second = subprocess.run(command, capture_output=True, timeout=5)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(output.read_bytes(), saved)


if __name__ == "__main__":
    unittest.main()
