# SPDX-License-Identifier: MIT
"""Bounded x64 PE runtime-function extents for existing exact static seeds."""
from collections import Counter
import hashlib
import re
import struct
import subprocess

from audit_profile import Image

MAX_ROWS = 500000
MAX_FUNCTION = 65536


def runtime_functions(image):
    data = image.data
    pe = struct.unpack_from("<I", data, 60)[0]
    optional = pe + 24
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    count = struct.unpack_from("<I", data, optional + 108)[0]
    if not 4 <= count <= 16 or optional_size < 112 + count * 8:
        raise ValueError("exception_directory_missing_or_truncated")
    rva, size = struct.unpack_from("<II", data, optional + 112 + 3 * 8)
    if not size or size % 12 or size // 12 > MAX_ROWS:
        raise ValueError("exception_directory_size_outside_bounds")
    section = image.section(rva, size, file_backed=True)
    if section is None or not section.readable or section.writable or section.executable:
        raise ValueError("exception_directory_not_readonly_metadata")
    records = []
    previous_end = 0
    for begin, end, unwind in struct.iter_unpack("<III", image.read(rva, size)):
        code = image.section(begin, end - begin, file_backed=True) if end > begin else None
        if (begin < previous_end or code is None or not code.executable or
                not unwind or unwind % 4 or image.section(unwind, 4, file_backed=True) is None):
            raise ValueError("invalid_overlapping_or_unsorted_runtime_function")
        records.append((begin, end, unwind))
        previous_end = end
    return records


def containing_function(records, seed):
    matches = [record for record in records if record[0] <= seed < record[1]]
    if len(matches) != 1 or matches[0][1] - matches[0][0] > MAX_FUNCTION:
        raise ValueError("seed_has_no_unique_bounded_runtime_function")
    return matches[0]


def static_seeds(image, profile):
    camera = profile["engineCamera"]
    references = image.matches(camera["cameraReferencePattern"])
    fingerprints = camera["cameraVtableFingerprints"]
    if not 2 <= len(fingerprints) <= 16:
        raise ValueError("invalid_seed_count")
    functions = [image.matches(item["pattern"]) for item in fingerprints]
    tables, reason = image.vtable_candidates(fingerprints, functions)
    if (len(references) != 1 or reason or len(tables) != 1 or
            image.reference_relationship(references, camera.get("cameraGlobalRva"))["status"] != "OBSERVED"):
        raise ValueError("prior_static_relationship_not_unique")
    return [("global_reference", references[0])] + [
        (f"vtable_slot_{item['slot']}", matches[0]) for item, matches in zip(fingerprints, functions)]


def parse_disassembly(text, begin, end):
    if len(text.encode("utf-8")) > 1024 * 1024:
        raise ValueError("disassembly_output_too_large")
    instructions = []
    for line in text.splitlines():
        match = re.fullmatch(r"\s*([0-9A-Fa-f]{8,16}):\s+([a-z][a-z0-9]*)\b.*", line)
        if match:
            address, mnemonic = int(match[1], 16), match[2]
            if not begin <= address < end or (instructions and address <= instructions[-1][0]):
                raise ValueError("disassembly_address_outside_ordered_range")
            instructions.append((address, mnemonic))
    if not instructions or instructions[0][0] != begin or len(instructions) > MAX_FUNCTION:
        raise ValueError("disassembly_did_not_start_at_function_entry")
    counts = dict(Counter(item[1] for item in instructions))
    return {"instruction_lines": len(instructions), "mnemonic_counts": counts,
            "scope": "Linear tool decoding only; not control-flow reachability, write coverage, function identity or producer semantics."}


def disassemble(tool, expected_hash, executable, image, record):
    if hashlib.sha256(tool.read_bytes()).hexdigest() != expected_hash:
        raise ValueError("disassembly_tool_changed")
    begin, end, _ = record
    if not 0 < end - begin <= MAX_FUNCTION:
        raise ValueError("function_size_outside_bounds")
    result = subprocess.run([str(tool), "/NOLOGO", "/DISASM:NOBYTES",
                             f"/RANGE:0x{image.image_base + begin:X},0x{image.image_base + end - 1:X}",
                             str(executable)], capture_output=True, timeout=10,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    if result.returncode or len(result.stdout) > 1024 * 1024 or result.stderr:
        raise ValueError("disassembly_tool_failed_or_output_rejected")
    if hashlib.sha256(tool.read_bytes()).hexdigest() != expected_hash:
        raise ValueError("disassembly_tool_changed_postflight")
    return parse_disassembly(result.stdout.decode("utf-8", errors="strict"),
                             image.image_base + begin, image.image_base + end)


def observe(image, seeds):
    records = runtime_functions(image)
    observations = []
    for role, seed in seeds:
        begin, end, unwind = containing_function(records, seed)
        version = image.read(unwind, 1)[0] & 7
        if version not in (1, 2):
            raise ValueError("unsupported_unwind_header_version")
        observations.append({"seed_role": role, "function_bytes": end - begin,
                             "seed_at_function_entry": seed == begin,
                             "unwind_header_version": version,
                             "function_sha256": hashlib.sha256(image.read(begin, end - begin)).hexdigest()})
    return {"runtime_function_records": len(records), "seeds": observations,
            "scope": "PE exception-directory ranges only. Unwind programs and reachability are not validated; absence of a record does not mean code is absent."}
