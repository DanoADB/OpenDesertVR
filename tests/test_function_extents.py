# SPDX-License-Identifier: MIT
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments/camera-profile-audit"))
from audit_profile import Image
from function_extents import containing_function, observe, parse_disassembly, runtime_functions


def fixture():
    data = bytearray(4096)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<HH", data, 132, 0x8664, 2)
    struct.pack_into("<H", data, 148, 240)
    struct.pack_into("<H", data, 152, 0x20b)
    struct.pack_into("<Q", data, 176, 0x140000000)
    struct.pack_into("<I", data, 208, 0x4000)
    struct.pack_into("<I", data, 260, 16)
    struct.pack_into("<II", data, 288, 0x2000, 24)
    for entry, rva, raw, flags in [(392, 0x1000, 1024, 0x60000020), (432, 0x2000, 2048, 0x40000040)]:
        struct.pack_into("<IIII", data, entry + 8, 1024, rva, 1024, raw)
        struct.pack_into("<I", data, entry + 36, flags)
    struct.pack_into("<IIIIII", data, 2048, 0x1000, 0x1010, 0x2040, 0x1020, 0x1040, 0x2044)
    data[2112] = data[2116] = 1
    return data


class FunctionExtentTests(unittest.TestCase):
    def test_metadata_ranges_are_half_open_and_seed_roles_are_not_function_identity(self):
        image = Image(fixture())
        rows = runtime_functions(image)
        self.assertEqual(containing_function(rows, 0x100f), (0x1000, 0x1010, 0x2040))
        with self.assertRaises(ValueError):
            containing_function(rows, 0x1010)
        result = observe(image, [("owned", 0x1008)])
        self.assertEqual(result["seeds"][0]["function_bytes"], 16)
        self.assertFalse(result["seeds"][0]["seed_at_function_entry"])
        self.assertNotIn("camera_verified", result)

    def test_rejects_directory_bounds_permissions_and_missing_entries(self):
        for offset, value in [(260, 3), (288, 0x3ff0), (292, 25), (292, 6000012), (432 + 36, 0xc0000040)]:
            data = fixture()
            struct.pack_into("<I", data, offset, value)
            with self.assertRaises(ValueError):
                runtime_functions(Image(data))

    def test_rejects_overlapping_unordered_outside_code_and_unwind_records(self):
        for offset, value in [(2060, 0x1008), (2060, 0xff0), (2052, 0x3000), (2056, 0x2041), (2056, 0x5000)]:
            data = fixture()
            struct.pack_into("<I", data, offset, value)
            with self.assertRaises(ValueError):
                runtime_functions(Image(data))
        data = fixture()
        data[2112] = 7
        with self.assertRaises(ValueError):
            observe(Image(data), [("owned", 0x1000)])

    def test_disassembly_accepts_only_ordered_in_range_instruction_lines_and_omits_operands(self):
        text = 'header\n  0000000140001000: mov eax,1\n  0000000140001005: ret\n'
        result = parse_disassembly(text, 0x140001000, 0x140001006)
        self.assertEqual(result["mnemonic_counts"], {"mov": 1, "ret": 1})
        self.assertNotIn("eax", str(result))
        for bad in ('header', text.replace('1005:', '1000:'), text.replace('1005:', '1006:'), text.replace('1000:', '1001:')):
            with self.assertRaises(ValueError):
                parse_disassembly(bad, 0x140001000, 0x140001006)


if __name__ == "__main__":
    unittest.main()
