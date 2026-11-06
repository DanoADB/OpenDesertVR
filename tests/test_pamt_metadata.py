# SPDX-License-Identifier: MIT
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

directory = Path(__file__).resolve().parents[1] / "experiments/shader-consumer-audit"
sys.path.insert(0, str(directory))
from pamt_metadata import Names, resolve_metadata, summary


def record(text, parent=0xffffffff, compact=False, offset=0):
    raw = text.encode("utf-8")
    prefix = struct.pack("<I", parent)
    if compact:
        distance = 0 if parent == 0xffffffff else offset - parent
        prefix = bytearray()
        while distance >= 128:
            prefix.append((distance & 127) | 128)
            distance >>= 7
        prefix.append(distance)
    return bytes(prefix) + bytes([len(raw)]) + raw


def fixture(compact=False, overlapping=False):
    directories = record("shadercache__/", compact=compact)
    child = len(directories)
    directories += record("d3d12/", parent=0, compact=compact, offset=child)
    other = len(directories)
    directories += record("other/", compact=compact)
    filenames = record("owned_a" if compact else "owned_a.padxil", compact=compact)
    second = len(filenames)
    filenames += record("owned_b" if compact else "owned_b.pars", compact=compact)
    folders = struct.pack("<IIII", 0, child, 0, 2)
    if overlapping:
        folders += struct.pack("<IIII", 0, other, 1, 1)
    files = (struct.pack("<IIIIHH", 0, 100, 80, 80, 0, 0x100 if compact else 0) +
             struct.pack("<IIIIHH", second, 180, 40, 80, 0, 0x212 if compact else 0x12))
    data = struct.pack("<IIIIII", 0, 1, 0, 0, 0, 0)
    for value, stride in ((directories, 1), (filenames, 1), (folders, 16), (files, 20)):
        data += struct.pack("<I", len(value) // stride) + value
    if compact:
        extensions = b"\0.padxil\0.pars\0"
        data += struct.pack("<I", len(extensions)) + extensions
    return data


class MetadataTests(unittest.TestCase):
    def test_owned_legacy_and_compact_indices_resolve_the_same_directory(self):
        for compact in (False, True):
            metadata = resolve_metadata(fixture(compact))
            self.assertEqual(metadata["format"], "compact" if compact else "legacy")
            self.assertEqual(metadata["selected_entries"], 2)
            self.assertEqual(metadata["extension_classes"], {"padxil": 1, "pars": 1})
            self.assertEqual(metadata["metadata_flags"], {"unencrypted": 1, "uncompressed": 1,
                                                       "encrypted": 1, "compressed": 1})
            self.assertEqual(metadata["minimum_stored_bytes"], 40)
            self.assertNotIn("_entries", summary(metadata))
            self.assertNotIn("owned_a", str(summary(metadata)))

    def test_parent_cycles_truncation_utf8_and_traversal_rejected(self):
        for block, compact in [(b"\0\0\0\0\x01a", False), (b"\xff" * 5, True),
                               (b"\x01\x01a", True), (record("abc")[:-1], False),
                               (b"\xff\xff\xff\xff\x01\xff", False),
                               (record("../camera"), False), (record("C:/camera"), False),
                               (record("/camera"), False)]:
            with self.subTest(block=block, compact=compact), self.assertRaises(ValueError):
                Names(block, compact).resolve(0)

    def test_oversized_paths_depth_and_truncated_metadata_rejected(self):
        block, parent = b"", 0xffffffff
        for _ in range(65):
            offset = len(block)
            block += record("a", parent=parent)
            parent = offset
        with self.assertRaises(ValueError):
            Names(block, False).resolve(parent)
        for cut in (1, 4, 12, 24, 40):
            with self.assertRaises(ValueError):
                resolve_metadata(fixture()[:-cut])
        with self.assertRaisesRegex(ValueError, "overlapping"):
            resolve_metadata(fixture(overlapping=True))

    def test_bad_entry_archive_name_size_extension_and_duplicate_rejected(self):
        # Both entry records are the last forty bytes in the legacy fixture.
        base = fixture()
        for relative, fmt, value in [(16, "H", 1), (0, "I", 0xfffffffe),
                                     (8, "I", 0), (12, "I", 0x7fffffff), (20, "I", 0)]:
            data = bytearray(base)
            struct.pack_into("<" + fmt, data, len(data) - 40 + relative, value)
            with self.subTest(relative=relative), self.assertRaises(ValueError):
                resolve_metadata(data)
        compact = bytearray(fixture(True))
        extension_size = len(b"\0.padxil\0.pars\0") + 4
        struct.pack_into("<H", compact, len(compact) - extension_size - 40 + 18, 0xff00)
        with self.assertRaisesRegex(ValueError, "extension"):
            resolve_metadata(compact)
        with self.assertRaises(ValueError):
            resolve_metadata(fixture(True) + b"extra")

    def test_cli_rejection_is_sanitized_and_refuses_existing_evidence(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output = root / "report.json"
            command = [sys.executable, str(directory / "resolve_shader_metadata.py"),
                       "--game-root", str(root), "--steam-manifest", str(root / "missing"),
                       "--index", str(root / "wrong"), "--index-sha256", "0" * 64, "--output", str(output)]
            first = subprocess.run(command, capture_output=True, timeout=5)
            self.assertEqual(first.returncode, 3)
            saved = output.read_bytes()
            self.assertNotIn(str(root).encode(), saved)
            second = subprocess.run(command, capture_output=True, timeout=5)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(output.read_bytes(), saved)


if __name__ == "__main__":
    unittest.main()
