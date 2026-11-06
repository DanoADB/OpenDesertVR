# SPDX-License-Identifier: MIT
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments/shader-consumer-audit"))
from shader_container import MAX_SHADER, chunks, parse_pasc


def sdk_dxc():
    root = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Windows Kits/10/bin"
    matches = sorted(root.glob("*/x64/dxc.exe"))
    return matches[-1] if matches else None


def dxbc(parts):
    start = 32 + 4 * len(parts)
    data, offsets = bytearray(), []
    for tag, payload in parts:
        while (start + len(data)) % 4:
            data += b"\0"
        offsets.append(start + len(data))
        data += tag + struct.pack("<I", len(payload)) + payload
    return (b"DXBC" + bytes(16) + struct.pack("<III", 1, start + len(data), len(parts)) +
            struct.pack("<" + "I" * len(parts), *offsets) + data)


def fixture(reflection=True):
    # Header fixture only; deliberately not executable/validated LLVM IR.
    program = struct.pack("<6I", (5 << 16) | 0x66, 7, 0x4c495844, 0x106, 16, 4) + b"BC\xc0\xde"
    parts = [(b"HASH", struct.pack("<I", 0) + b"h" * 16), (b"DXIL", program)]
    if reflection:
        parts.append((b"STAT", program))
    container = dxbc(parts)
    return b"PASC" + struct.pack("<4I", 8, 36 + len(container), 17, 36) + b"h" * 16 + container


class ContainerTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt" and sdk_dxc(), "Windows SDK DXC required for owned compile fixture")
    def test_installed_sdk_compiles_original_fixture_and_reader_accepts_its_real_container(self):
        source = Path(__file__).resolve().parents[1] / "experiments/shader-consumer-audit/owned_consumer.hlsl"
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "owned.dxil"
            built = subprocess.run([str(sdk_dxc()), "-T", "cs_6_0", "-E", "main", "-Fo", str(path), str(source)],
                                   capture_output=True, timeout=30)
            self.assertEqual(built.returncode, 0, built.stderr.decode("utf-8", errors="replace"))
            binary = path.read_bytes()
            digest = chunks(binary)[b"HASH"][4:20]
            wrapped = b"PASC" + struct.pack("<4I", 8, len(binary) + 36, 17, 36) + digest + binary
            raw, report = parse_pasc(wrapped)
            self.assertEqual(raw, binary)
            self.assertEqual(report["shader_kind"], "compute")
            self.assertEqual(report["shader_model_minor"], 0)
            self.assertTrue(report["reflection_bitcode_present"])

    def test_structure_reports_metadata_and_does_not_infer_reflection_contents(self):
        for present in (False, True):
            container, report = parse_pasc(fixture(present))
            self.assertEqual(report["shader_kind"], "compute")
            self.assertEqual(report["shader_model_minor"], 6)
            self.assertEqual(report["reflection_bitcode_present"], present)
            self.assertEqual(report["container_bytes"], len(container))
            self.assertNotIn("payload", report)
            self.assertIn("not validated", report["scope"])

    def test_wrapper_versions_lengths_offsets_and_digest_must_match(self):
        for offset, value in ((4, 7), (8, 1), (16, 40)):
            bad = bytearray(fixture())
            struct.pack_into("<I", bad, offset, value)
            with self.assertRaises(ValueError):
                parse_pasc(bad)
        bad = bytearray(fixture())
        bad[20] ^= 1
        with self.assertRaisesRegex(ValueError, "digest"):
            parse_pasc(bad)
        for data in (fixture()[:-1], b"", bytes(MAX_SHADER + 1)):
            with self.assertRaises(ValueError):
                parse_pasc(data)

    def test_duplicate_overlapping_unaligned_and_out_of_bounds_parts_rejected(self):
        with self.assertRaises(ValueError):
            chunks(dxbc([(b"HASH", bytes(20)), (b"HASH", bytes(20))]))
        base = fixture()[36:]
        for offset in (0, 33, len(base) - 4, 0xffffffff):
            bad = bytearray(base)
            struct.pack_into("<I", bad, 32, offset)
            with self.assertRaises(ValueError):
                chunks(bad)
        bad = bytearray(base)
        first = struct.unpack_from("<I", bad, 32)[0]
        second = struct.unpack_from("<I", bad, 36)[0]
        struct.pack_into("<I", bad, first + 4, second - first + 4)
        with self.assertRaisesRegex(ValueError, "overlapping"):
            chunks(bad)

    def test_invalid_program_size_magic_bitcode_span_and_model_rejected(self):
        for word, value in ((0, (5 << 16) | 0x56), (1, 8), (2, 0), (4, 0), (5, 0xffffffff)):
            bad = bytearray(fixture())
            dxil = 36 + struct.unpack_from("<I", bad, 36 + 36)[0] + 8
            struct.pack_into("<I", bad, dxil + word * 4, value)
            with self.assertRaises(ValueError):
                parse_pasc(bad)


if __name__ == "__main__":
    unittest.main()
