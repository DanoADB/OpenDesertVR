# SPDX-License-Identifier: MIT
"""Offline instruction-guard observations only; never reads a running process."""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct


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
        self.sections = []
        for index in range(count):
            entry = table + index * 40
            virtual_size, rva, size, raw = struct.unpack_from("<IIII", data, entry + 8)
            flags = struct.unpack_from("<I", data, entry + 36)[0]
            if raw + size > len(data) or rva + max(size, virtual_size) > 0xffffffff:
                raise ValueError("Section exceeds image bounds")
            if size:
                if any(raw < other[0] + other[1] and other[0] < raw + size for other in self.sections):
                    raise ValueError("Overlapping raw sections")
                self.sections.append((raw, size, rva, bool(flags & 0x20000000)))

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
        for raw, size, rva, executable in self.sections:
            if not executable:
                continue
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


def observe(data, profile):
    image = Image(data)
    camera = profile["engineCamera"]
    references = image.matches(camera["cameraReferencePattern"])
    fingerprints = camera.get("cameraVtableFingerprints", [])
    if not fingerprints:
        raise ValueError("Profile lacks camera function fingerprints")
    return {
        "camera_reference_matches_in_executable_sections": len(references),
        "reference_matches_at_published_rva": camera["cameraReferenceRva"] in references,
        "vtable_function_fingerprint_match_counts": [len(image.matches(item["pattern"])) for item in fingerprints],
        "scope": "Pattern occurrences only. Does not resolve a vtable, validate a live object, relocate a profile or grant runtime compatibility."
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
