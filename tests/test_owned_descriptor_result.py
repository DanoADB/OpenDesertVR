# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import tempfile
import unittest

path = Path(__file__).resolve().parents[1] / "experiments/renodx-owned/qualify_descriptor.py"
spec = importlib.util.spec_from_file_location("owned_descriptor", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class OwnedDescriptorResultTests(unittest.TestCase):
    def test_exact_completed_result_is_required_not_just_pass_marker(self):
        good = b"PASS\nlayouts=64\nthreads_per_layout=16\nsuccessful_requests=1024\nfailed_requests=0\ndivergent_layouts=0\n"
        self.assertEqual(module.parse_result(good), module.EXPECTED)
        for bad in (b"PASS\n", good.replace(b"=1024", b"=1023"), good.replace(b"failed_requests=0", b"failed_requests=1"), good + b"extra=0\n"):
            with self.assertRaises(ValueError):
                module.parse_result(bad)

    def test_duplicate_unknown_or_non_integer_records_cannot_masquerade_as_completion(self):
        data = b"PASS\nlayouts=64\nlayouts=64\nsuccessful_requests=1024\nfailed_requests=0\ndivergent_layouts=0\n"
        for bad in (data, data.replace(b"layouts=64", b"layouts=true"), data.replace(b"layouts=64", b"layouts=-64"), b"x" * 4097):
            with self.assertRaises(ValueError):
                module.parse_result(bad)

    def test_pinned_input_checks_reject_changes_and_paths_outside_reference_root(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / "reference"
            root.mkdir()
            owned = root / "owned.hpp"
            owned.write_bytes(b"owned")
            lock = {"files": [{"path": "owned.hpp", "sha256": module.digest(owned)}]}
            module.verify_sources(root, lock)
            owned.write_bytes(b"changed")
            with self.assertRaises(ValueError):
                module.verify_sources(root, lock)
            outside = Path(folder) / "outside.hpp"
            outside.write_bytes(b"owned")
            with self.assertRaises(ValueError):
                module.verify_sources(root, {"files": [{"path": "../outside.hpp", "sha256": module.digest(outside)}]})


if __name__ == "__main__":
    unittest.main()
