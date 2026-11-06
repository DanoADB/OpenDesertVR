# SPDX-License-Identifier: MIT
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

script = Path(__file__).resolve().parents[1] / "experiments/camera-profile-audit/association.py"
spec = importlib.util.spec_from_file_location("camera_association", script)
association = importlib.util.module_from_spec(spec)
spec.loader.exec_module(association)


class AssociationTests(unittest.TestCase):
    def test_stable_association_is_bounded_and_does_not_export_addresses(self):
        calls = []

        def read(address):
            calls.append(address)
            return {100: 200, 200: 300}[address]

        report = association.sample_association(read, 100, 300)
        self.assertEqual(calls, [100, 200, 200, 100] * 3)
        self.assertEqual(report["bytes_read"], 96)
        self.assertEqual(report["status"], "OBSERVED")
        self.assertNotIn("address", report)
        self.assertNotIn("compatible", report)

    def test_null_and_mismatched_objects_stop_immediately(self):
        for values, expected_count, reason in [([0], 1, "global_unavailable"),
                                                ([200, 999, 999, 200], 4, "vtable_mismatch")]:
            iterator = iter(values)
            calls = []

            def read(address):
                calls.append(address)
                return next(iterator)

            with self.assertRaisesRegex(association.ObservationError, reason):
                association.sample_association(read, 100, 300)
            self.assertEqual(len(calls), expected_count)

    def test_changes_within_or_between_samples_are_not_accepted(self):
        for values, reason in [([200, 300, 301, 200], "source_changed_during_sample"),
                               ([200, 300, 300, 201], "source_changed_during_sample"),
                               ([200, 300, 300, 200, 400, 300, 300, 400], "source_replaced_between_samples")]:
            iterator = iter(values)
            with self.assertRaisesRegex(association.ObservationError, reason):
                association.sample_association(lambda _: next(iterator), 100, 300)

    def test_read_failure_is_not_retried(self):
        calls = []

        def read(address):
            calls.append(address)
            raise association.ObservationError("pointer_read_failed")

        with self.assertRaisesRegex(association.ObservationError, "pointer_read_failed"):
            association.sample_association(read, 100, 300)
        self.assertEqual(calls, [100])

    def test_cli_rejects_before_process_access_and_preserves_existing_output(self):
        probe = script.with_name("probe_association.py")
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            report = root / "report.json"
            command = [sys.executable, str(probe), "--pid", "1", "--executable", str(root / "wrong.exe"),
                       "--profile", str(root / "missing-profile"), "--steam-manifest", str(root / "missing-manifest"),
                       "--output", str(report)]
            rejected = subprocess.run(command, capture_output=True, text=True, timeout=5)
            self.assertEqual(rejected.returncode, 3)
            result = json.loads(report.read_text())
            self.assertEqual(result["failure_reason"], "unexpected_executable_name")
            self.assertEqual(result["process_memory_access"], "NOT_RUN")
            before = report.read_bytes()
            second = subprocess.run(command, capture_output=True, text=True, timeout=5)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(report.read_bytes(), before)

    @unittest.skipUnless(os.name == "nt", "Actual cross-process fixture requires Windows")
    def test_native_read_only_api_on_owned_child(self):
        source = """
import ctypes, json, sys
table = (ctypes.c_uint64 * 3)(0, 0, 0)
candidate = ctypes.c_uint64(ctypes.addressof(table))
root = ctypes.c_uint64(ctypes.addressof(candidate))
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]
kernel.VirtualAlloc.restype = ctypes.c_void_p
blocked = kernel.VirtualAlloc(None, 4096, 0x3000, 1)
if not blocked: raise RuntimeError('owned allocation failed')
print(json.dumps({'root': ctypes.addressof(root), 'table': ctypes.addressof(table), 'blocked': blocked}), flush=True)
sys.stdin.buffer.read(1)
"""
        child = subprocess.Popen([sys.executable, "-c", source], stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            context = json.loads(child.stdout.readline())
            with association.ReadOnlyProcess(child.pid, sys.executable) as reader:
                self.assertGreater(reader.base, 0)
                self.assertGreater(reader.image_size, 0)
                self.assertFalse(reader.rights & (0x20 | 0x8 | 0x2 | 0x1))
                result = association.sample_association(reader.read_pointer, context["root"], context["table"])
                self.assertEqual(result["status"], "OBSERVED")
                with self.assertRaisesRegex(association.ObservationError, "memory_not_readable"):
                    reader.read_pointer(context["blocked"])
                with self.assertRaisesRegex(association.ObservationError, "invalid_pointer"):
                    reader.read_pointer(0)
            self.assertIsNone(reader.handle)
            with self.assertRaisesRegex(association.ObservationError, "process_path_mismatch"):
                association.ReadOnlyProcess(child.pid, Path(sys.executable).with_name("not-the-target.exe"))
        finally:
            child.stdin.close()  # Cooperative EOF; no process termination rights.
            child.wait(timeout=10)
            child.stdout.close()
            child.stderr.close()
        self.assertEqual(child.returncode, 0)


if __name__ == "__main__":
    unittest.main()
