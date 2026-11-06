# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import struct
import unittest

script = Path(__file__).resolve().parents[1] / "experiments/camera-profile-audit/audit_profile.py"
spec = importlib.util.spec_from_file_location("camera_audit", script)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def image():
    data = bytearray(2048)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<HH", data, 132, 0x8664, 2)
    struct.pack_into("<H", data, 148, 112)
    struct.pack_into("<H", data, 152, 0x20b)
    for entry, rva, raw, flags in [(264, 4096, 512, 0x60000020), (304, 8192, 1024, 0x40000040)]:
        struct.pack_into("<IIII", data, entry + 8, 512, rva, 512, raw)
        struct.pack_into("<I", data, entry + 36, flags)
    return data


class CameraAuditTests(unittest.TestCase):
    def test_ignores_non_executable_lookalike_and_preserves_rva(self):
        data = image()
        data[520:527] = data[1032:1039] = b"ABCDEFG"
        self.assertEqual(audit.Image(data).matches("41 42 ?? 44 45 46 47"), [4104])

    def test_reports_ambiguity_and_excludes_cross_section_match(self):
        data = image()
        data[520:527] = data[540:547] = data[1020:1027] = b"ABCDEFG"
        self.assertEqual(audit.Image(data).matches("41 42 43 44 45 46 47"), [4104, 4124])

    def test_later_fixed_run_with_leading_wildcards(self):
        data = image()
        data[520:528] = b"XYABCDEF"
        self.assertEqual(audit.Image(data).matches("?? ?? 41 42 43 44 45 46"), [4104])

    def test_rejects_unbounded_and_invalid_headers(self):
        for position, value in [(60, 0xffffff00), (264 + 20, 2000), (304 + 20, 512)]:
            data = image()
            struct.pack_into("<I", data, position, value)
            with self.assertRaises(ValueError):
                audit.Image(data)

    def test_rejects_vacuous_or_malformed_guards(self):
        for pattern in ["?? " * 7, "GG " * 7, "41 42", "4? " * 7]:
            with self.assertRaises(ValueError):
                audit.Image(image()).matches(pattern)

    def test_observation_cannot_award_runtime_compatibility(self):
        data = image()
        data[520:527] = b"ABCDEFG"
        profile = {"engineCamera": {"cameraReferencePattern": "41 42 ?? 44 45 46 47",
                   "cameraReferenceRva": 4104,
                   "cameraVtableFingerprints": [{"slot": 1, "pattern": "41 42 ?? 44 45 46 47"}]}}
        report = audit.observe(data, profile)
        self.assertTrue(report["reference_matches_at_published_rva"])
        self.assertNotIn("compatible", report)
        self.assertNotIn("cameraReferenceRva", report)


if __name__ == "__main__":
    unittest.main()
