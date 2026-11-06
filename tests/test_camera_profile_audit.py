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
    struct.pack_into("<Q", data, 176, 0x140000000)
    struct.pack_into("<I", data, 208, 12288)
    for entry, rva, raw, flags in [(264, 4096, 512, 0x60000020), (304, 8192, 1024, 0x40000040)]:
        struct.pack_into("<IIII", data, entry + 8, 512, rva, 512, raw)
        struct.pack_into("<I", data, entry + 36, flags)
    return data


def relationship_fixture():
    data = image()
    struct.pack_into("<H", data, 134, 3)
    struct.pack_into("<I", data, 208, 16384)
    for entry, rva, raw, flags in [(264, 12288, 512, 0x60000020),
                                   (304, 8192, 1024, 0x40000040),
                                   (344, 4096, 1536, 0xc0000040)]:
        struct.pack_into("<IIII", data, entry + 8, 512, rva, 512, raw)
        struct.pack_into("<I", data, entry + 36, flags)
    data[520:523] = b"\x48\x8b\x05"
    struct.pack_into("<i", data, 523, 4104 - (12296 + 7))
    data[576:583], data[640:647] = b"ABCDEFG", b"HIJKLMN"
    struct.pack_into("<QQ", data, 1096, 0x140003040, 0x140003080)
    profile = {"engineCamera": {
        "cameraReferenceRva": 12296, "cameraReferencePattern": "48 8B 05 ?? ?? ?? ??",
        "cameraGlobalRva": 4104, "cameraVtableRva": 8256,
        "cameraVtableFingerprints": [{"slot": 1, "pattern": "41 42 43 44 45 46 47"},
                                     {"slot": 2, "pattern": "48 49 4A 4B 4C 4D 4E"}]}}
    return data, profile


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
                   "cameraVtableFingerprints": [{"slot": 1, "pattern": "41 42 ?? 44 45 46 47"},
                                                {"slot": 2, "pattern": "41 42 ?? 44 45 46 47"}]}}
        report = audit.observe(data, profile)
        self.assertTrue(report["reference_matches_at_published_rva"])
        self.assertNotIn("compatible", report)
        self.assertNotIn("cameraReferenceRva", report)

    def test_negative_rip_displacement_and_ordered_pointer_table(self):
        data, profile = relationship_fixture()
        result = audit.observe(data, profile)
        self.assertEqual(result["reference_relationship"]["status"], "OBSERVED")
        self.assertTrue(result["reference_relationship"]["target_equals_published_global_rva"])
        table = result["vtable_relationship"]
        self.assertEqual(table["status"], "OBSERVED_UNIQUE")
        self.assertTrue(table["table_equals_published_vtable_rva"])
        self.assertNotIn("rva", table)

    def test_duplicate_tables_are_reported_as_ambiguous(self):
        data, profile = relationship_fixture()
        struct.pack_into("<QQ", data, 1288, 0x140003040, 0x140003080)
        result = audit.observe(data, profile)["vtable_relationship"]
        self.assertEqual(result["status"], "AMBIGUOUS")
        self.assertEqual(result["matching_readonly_pointer_tables"], 2)

    def test_writable_table_is_not_accepted(self):
        data, profile = relationship_fixture()
        struct.pack_into("<I", data, 304 + 36, 0xc0000040)
        self.assertEqual(audit.observe(data, profile)["vtable_relationship"]["status"], "MISSING")

    def test_global_must_be_writable_data(self):
        data, profile = relationship_fixture()
        struct.pack_into("<I", data, 344 + 36, 0x40000040)
        self.assertEqual(audit.observe(data, profile)["reference_relationship"]["status"], "INVALID")

    def test_zero_filled_global_is_valid_metadata_but_not_readable_file_data(self):
        data, profile = relationship_fixture()
        struct.pack_into("<II", data, 344 + 16, 0, 0)
        self.assertEqual(audit.observe(data, profile)["reference_relationship"]["status"], "OBSERVED")
        with self.assertRaises(ValueError):
            audit.Image(data).read(4104, 8)

    def test_overlapping_virtual_sections_rejected(self):
        data = image()
        struct.pack_into("<I", data, 304 + 12, 4096)
        with self.assertRaises(ValueError):
            audit.Image(data)

    def test_table_crossing_section_end_rejected(self):
        data, profile = relationship_fixture()
        data[1096:1112] = bytes(16)
        struct.pack_into("<QQ", data, 1528, 0x140003040, 0x140003080)
        self.assertEqual(audit.observe(data, profile)["vtable_relationship"]["status"], "MISSING")

    def test_duplicate_slots_and_nonunique_functions_cannot_resolve_table(self):
        data, profile = relationship_fixture()
        profile["engineCamera"]["cameraVtableFingerprints"][1]["slot"] = 1
        with self.assertRaises(ValueError):
            audit.observe(data, profile)
        data, profile = relationship_fixture()
        data[700:707] = b"ABCDEFG"
        self.assertEqual(audit.observe(data, profile)["vtable_relationship"]["status"], "NOT_RUN")


if __name__ == "__main__":
    unittest.main()
