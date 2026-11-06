# SPDX-License-Identifier: MIT
# PASC v8 layout informed by CDMW-Full 0dfc3b1 (MIT, Ratrider 2026).
# DXIL container/program layouts follow Microsoft's DirectXShaderCompiler
# DxilContainer.h. Original bounded reader; no shader/bitcode execution.
"""Structural metadata for one PASC v8/DXIL container; not a shader validator."""
import hashlib
import struct

MAX_SHADER = 1024 * 1024
KINDS = ("pixel", "vertex", "geometry", "hull", "domain", "compute", "library",
         "ray_generation", "intersection", "any_hit", "closest_hit", "miss", "callable",
         "mesh", "amplification", "node")


class ContainerError(ValueError):
    pass


def chunks(data):
    if not 32 <= len(data) <= MAX_SHADER or data[:4] != b"DXBC":
        raise ContainerError("invalid_dxbc_size_or_magic")
    data = bytes(data)
    version, total, count = struct.unpack_from("<III", data, 20)
    if version != 1 or total != len(data) or not 1 <= count <= 64 or 32 + count * 4 > total:
        raise ContainerError("invalid_dxbc_header")
    result, intervals = {}, []
    for i in range(count):
        offset = struct.unpack_from("<I", data, 32 + 4 * i)[0]
        if offset % 4 or offset < 32 + 4 * count or offset + 8 > total:
            raise ContainerError("invalid_part_offset")
        tag = data[offset:offset + 4]
        size = struct.unpack_from("<I", data, offset + 4)[0]
        if (offset + 8 + size > total or tag in result or
                any(not (48 <= char <= 57 or 65 <= char <= 90) for char in tag)):
            raise ContainerError("invalid_duplicate_or_truncated_part")
        intervals.append((offset, offset + 8 + size))
        result[tag] = data[offset + 8:offset + 8 + size]
    intervals.sort()
    if any(left[1] > right[0] for left, right in zip(intervals, intervals[1:])):
        raise ContainerError("overlapping_container_parts")
    return result


def program_header(data):
    if len(data) < 24 or len(data) % 4:
        raise ContainerError("invalid_program_size")
    version, dwords, magic, dxil_version, bitcode_offset, bitcode_size = struct.unpack_from("<6I", data)
    start = 8 + bitcode_offset  # Relative to the embedded bitcode header.
    if (dwords * 4 != len(data) or magic != 0x4c495844 or bitcode_offset < 16 or
            bitcode_size < 4 or start + bitcode_size > len(data) or data[start:start + 4] != b"BC\xc0\xde"):
        raise ContainerError("invalid_bitcode_header_or_span")
    kind = version >> 16
    major, minor = (version >> 4) & 15, version & 15
    if kind >= len(KINDS) or major != 6:
        raise ContainerError("unsupported_shader_kind_or_model")
    return {"shader_kind": KINDS[kind], "shader_model_major": major, "shader_model_minor": minor,
            "dxil_version": dxil_version, "bitcode_bytes": bitcode_size}


def parse_pasc(data):
    if not 68 <= len(data) <= MAX_SHADER or data[:4] != b"PASC":
        raise ContainerError("invalid_pasc_size_or_magic")
    data = bytes(data)
    version, total, _, start = struct.unpack_from("<IIII", data, 4)
    if version != 8 or total != len(data) or start != 36:
        raise ContainerError("unsupported_pasc_header")
    container = data[start:]
    parts = chunks(container)
    if b"DXIL" not in parts or b"HASH" not in parts or len(parts[b"HASH"]) != 20:
        raise ContainerError("required_program_or_hash_missing")
    if data[20:36] != parts[b"HASH"][4:20]:
        raise ContainerError("wrapper_and_shader_digest_disagree")
    program = program_header(parts[b"DXIL"])
    if b"STAT" in parts:
        reflection = program_header(parts[b"STAT"])
        if any(reflection[key] != program[key] for key in
               ("shader_kind", "shader_model_major", "shader_model_minor")):
            raise ContainerError("reflection_program_identity_disagrees")
    report = {"status": "OBSERVED_CONTAINER_STRUCTURE", "pasc_version": 8,
              "pasc_bytes": len(data), "pasc_sha256": hashlib.sha256(data).hexdigest(),
              "container_bytes": len(container), "container_sha256": hashlib.sha256(container).hexdigest(),
              "chunk_count": len(parts), "chunk_types": [tag.decode("ascii") for tag in parts],
              **program, "reflection_bitcode_present": b"STAT" in parts,
              "runtime_data_present": b"RDAT" in parts,
              "scope": "Bounded structural metadata and opaque digest agreement only. Bitcode, compiler validity, reflection contents, active bindings, camera fields and producer semantics are not validated."}
    return container, report
