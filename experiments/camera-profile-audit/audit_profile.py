# SPDX-License-Identifier: MIT
"""Offline instruction-guard observations only; never reads a running process."""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct
from typing import NamedTuple


class Section(NamedTuple):
    raw: int
    size: int
    rva: int
    extent: int
    flags: int

    @property
    def executable(self):
        return bool(self.flags & 0x20000000)

    @property
    def readable(self):
        return bool(self.flags & 0x40000000)

    @property
    def writable(self):
        return bool(self.flags & 0x80000000)


class Image:
    def __init__(self, data):
        self.data = data
        if len(data) < 64 or data[:2] != b"MZ":
            raise ValueError("Expected a PE image")
        pe = struct.unpack_from("<I", data, 60)[0]
        if pe > len(data) - 24 or data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("Invalid PE header")
        machine, count = struct.unpack_from("<HH", data, pe + 4)
        optional_size = struct.unpack_from("<H", data, pe + 20)[0]
        table = pe + 24 + optional_size
        if (machine != 0x8664 or not 1 <= count <= 96 or optional_size < 112 or
                table + count * 40 > len(data) or struct.unpack_from("<H", data, pe + 24)[0] != 0x20b):
            raise ValueError("Expected a bounded x64 PE32+ section table")
        self.image_base = struct.unpack_from("<Q", data, pe + 48)[0]
        image_size = struct.unpack_from("<I", data, pe + 80)[0]
        self.image_size = image_size
        if not image_size or self.image_base + image_size > 0x10000000000000000:
            raise ValueError("Invalid preferred image range")
        self.sections = []
        for index in range(count):
            entry = table + index * 40
            virtual_size, rva, size, raw = struct.unpack_from("<IIII", data, entry + 8)
            flags = struct.unpack_from("<I", data, entry + 36)[0]
            extent = max(size, virtual_size)
            if (size and (raw < table + count * 40 or raw + size > len(data))) or rva + extent > image_size:
                raise ValueError("Section exceeds image bounds")
            if extent:
                if size and any(other.size and raw < other.raw + other.size and other.raw < raw + size
                                for other in self.sections):
                    raise ValueError("Overlapping raw sections")
                if any(rva < other.rva + other.extent and other.rva < rva + extent for other in self.sections):
                    raise ValueError("Overlapping virtual sections")
                self.sections.append(Section(raw, size, rva, extent, flags))

    def section(self, rva, size, file_backed=False):
        if rva < 0 or size <= 0:
            return None
        return next((s for s in self.sections if s.rva <= rva and
                     rva + size <= s.rva + (s.size if file_backed else s.extent)), None)

    def read(self, rva, size):
        section = self.section(rva, size, file_backed=True)
        if section is None:
            raise ValueError("RVA is not entirely file-backed")
        offset = section.raw + rva - section.rva
        return self.data[offset:offset + size]

    def matches(self, pattern):
        tokens = pattern.split()
        if not 7 <= len(tokens) <= 128:
            raise ValueError("Guard must contain 7..128 tokens")
        values = []
        for token in tokens:
            if token == "??":
                values.append(None)
            elif len(token) == 2 and all(c in "0123456789abcdefABCDEF" for c in token):
                values.append(int(token, 16))
            else:
                raise ValueError("Invalid guard token")
        concrete = [(i, value) for i, value in enumerate(values) if value is not None]
        if not concrete:
            raise ValueError("An all-wildcard guard is not evidence")
        # Search the longest fixed run first; common one-byte opcodes would
        # otherwise require millions of Python-level candidate comparisons.
        runs = []
        start = 0
        while start < len(values):
            if values[start] is None:
                start += 1
                continue
            end = start + 1
            while end < len(values) and values[end] is not None:
                end += 1
            runs.append((start, bytes(values[start:end])))
            start = end
        anchor_index, needle = max(runs, key=lambda run: len(run[1]))
        result = []
        for section in self.sections:
            if not section.executable:
                continue
            raw, size, rva = section.raw, section.size, section.rva
            cursor = raw + anchor_index
            while cursor < raw + size:
                hit = self.data.find(needle, cursor, raw + size)
                if hit < 0:
                    break
                start = hit - anchor_index
                if start >= raw and start + len(values) <= raw + size and all(
                        self.data[start + offset] == value for offset, value in concrete):
                    result.append(rva + start - raw)
                    if len(result) > 1024:
                        raise ValueError("Guard is too ambiguous")
                cursor = hit + 1
        return result

    def reference_relationship(self, references, published_target):
        if len(references) != 1:
            return {"status": "NOT_RUN", "reason": "reference_guard_not_unique"}
        instruction = references[0]
        code = self.read(instruction, 7)
        # The pinned profile guards this exact x64 RIP-relative MOV encoding.
        if code[:3] != b"\x48\x8b\x05":
            return {"status": "INVALID", "reason": "unexpected_reference_encoding"}
        target = instruction + 7 + struct.unpack_from("<i", code, 3)[0]
        section = self.section(target, 8)
        valid = (target % 8 == 0 and section is not None and section.readable and
                 section.writable and not section.executable)
        return {"status": "OBSERVED" if valid else "INVALID",
                "target_is_aligned_writable_data": valid,
                "target_equals_published_global_rva": target == published_target}

    def vtable_candidates(self, fingerprints, functions):
        slots = [item["slot"] for item in fingerprints]
        if (len(slots) < 2 or any(type(slot) is not int or not 0 <= slot <= 64 for slot in slots) or
                len(set(slots)) != len(slots)):
            raise ValueError("Require at least two distinct bounded vtable slots")
        if any(len(matches) != 1 for matches in functions):
            return None, "function_guards_not_unique"
        pointers = [self.image_base + matches[0] for matches in functions]
        if len(set(pointers)) < 2:
            return None, "function_guards_not_independent"
        anchor = struct.pack("<Q", pointers[0])
        table_size = (max(slots) + 1) * 8
        tables = []
        for section in self.sections:
            if not section.readable or section.writable or section.executable:
                continue
            cursor = section.raw
            while cursor < section.raw + section.size:
                hit = self.data.find(anchor, cursor, section.raw + section.size)
                if hit < 0:
                    break
                table = section.rva + hit - section.raw - slots[0] * 8
                if (table % 8 == 0 and self.section(table, table_size, file_backed=True) == section and
                        all(struct.unpack("<Q", self.read(table + slot * 8, 8))[0] == pointer
                            for slot, pointer in zip(slots, pointers))):
                    tables.append(table)
                    if len(tables) > 1024:
                        raise ValueError("Too many matching pointer tables")
                cursor = hit + 1
        return tables, None

    def vtable_relationship(self, fingerprints, functions, published_table):
        tables, reason = self.vtable_candidates(fingerprints, functions)
        if reason:
            return {"status": "NOT_RUN", "reason": reason}
        return {"status": "OBSERVED_UNIQUE" if len(tables) == 1 else "AMBIGUOUS" if tables else "MISSING",
                "matching_readonly_pointer_tables": len(tables),
                "table_equals_published_vtable_rva": published_table in tables,
                "scope": "Ordered function-pointer table only; C++ type and live camera association remain unverified."}


def observe(data, profile):
    image = Image(data)
    camera = profile["engineCamera"]
    references = image.matches(camera["cameraReferencePattern"])
    fingerprints = camera.get("cameraVtableFingerprints", [])
    if not 1 <= len(fingerprints) <= 16:
        raise ValueError("Require 1..16 camera function fingerprints")
    functions = [image.matches(item["pattern"]) for item in fingerprints]
    return {
        "camera_reference_matches_in_executable_sections": len(references),
        "reference_matches_at_published_rva": camera["cameraReferenceRva"] in references,
        "vtable_function_fingerprint_match_counts": [len(matches) for matches in functions],
        "reference_relationship": image.reference_relationship(references, camera.get("cameraGlobalRva")),
        "vtable_relationship": image.vtable_relationship(fingerprints, functions, camera.get("cameraVtableRva")),
        "scope": "Static instruction/data relationships only. Does not validate a live object, relocate a profile or grant runtime compatibility."
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--expected-executable-sha256", required=True)
    parser.add_argument("--expected-profile-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("Use a fresh output file")
    if args.profile.stat().st_size > 256 * 1024:
        raise ValueError("Profile too large")
    profile_bytes = args.profile.read_bytes()
    profile_hash = hashlib.sha256(profile_bytes).hexdigest()
    if profile_hash != args.expected_profile_sha256.lower():
        raise ValueError("Profile fingerprint changed")
    profile = json.loads(profile_bytes)
    with args.executable.open("rb") as stream:
        if not 1 <= args.executable.stat().st_size <= 512 * 1024 * 1024:
            raise ValueError("Executable size outside audit bounds")
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
            executable_hash = hashlib.sha256(data).hexdigest()
            if executable_hash != args.expected_executable_sha256.lower():
                raise ValueError("Executable fingerprint changed")
            observation = observe(data, profile)
            if hashlib.sha256(data).hexdigest() != executable_hash:
                raise ValueError("Executable changed during observation")
    report = {"schema_version": 1, "evidence_kind": "offline_camera_guard_observation",
              "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "executable_sha256": executable_hash, "profile_sha256": profile_hash,
              "profile_steam_build_id": profile["steamBuildId"],
              "profile_executable_hash_matches": profile["executableSha256"].lower() == executable_hash,
              "observation": observation, "runtime_camera": "UNVERIFIED", "native_stereo": "UNVERIFIED",
              "game_execution": "NOT_RUN", "process_memory_access": "NOT_RUN"}
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")


if __name__ == "__main__":
    main()
