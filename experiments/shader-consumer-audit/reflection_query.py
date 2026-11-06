# SPDX-License-Identifier: MIT
"""Run the observer's reflection helper on bounded bytes, with file provenance."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from shader_container import chunks, program_header

QUERIES = {0x20, 0x30, 0x80, 0x90, 0x3e0, 0x420, 0x4e0, 0x860, 0xac0}


class ReflectionError(ValueError):
    pass


def digest(path, maximum):
    path = Path(path)
    if not path.is_file() or not 1 <= path.stat().st_size <= maximum:
        raise ReflectionError("reflection_tool_size_or_path_rejected")
    value, total = hashlib.sha256(), 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            total += len(block)
            if total > maximum:
                raise ReflectionError("reflection_tool_size_changed")
            value.update(block)
    return value.hexdigest()


def validate(result):
    if (not isinstance(result, dict) or type(result.get("schema_version")) is not int or result.get("schema_version") != 1 or
            result.get("status") != "OBSERVED_REFLECTION_METADATA"):
        raise ReflectionError("reflection_helper_rejected")
    for key, limit in (("constant_buffers", 64), ("bound_resources", 512), ("variables", 4096)):
        if type(result.get(key)) is not int or not 0 <= result[key] <= limit:
            raise ReflectionError("reflection_output_counts_rejected")
    matches = result.get("query_matches")
    if not isinstance(matches, list) or len(matches) > 512:
        raise ReflectionError("reflection_output_matches_rejected")
    fields = {"query_offset", "buffer_ordinal", "declaration_offset", "declaration_bytes", "exact_start",
              "type", "class", "rows", "columns", "elements", "members", "compiler_used"}
    for item in matches:
        if not isinstance(item, dict) or set(item) != fields:
            raise ReflectionError("reflection_output_schema_rejected")
        for key in ("query_offset", "buffer_ordinal", "declaration_offset", "declaration_bytes",
                    "class", "rows", "columns", "elements", "members"):
            if type(item[key]) is not int or not 0 <= item[key] <= 65536:
                raise ReflectionError("reflection_output_value_rejected")
        if (item["query_offset"] not in QUERIES or item["buffer_ordinal"] >= 64 or
                item["declaration_offset"] + item["declaration_bytes"] > 65536 or
                not item["declaration_offset"] <= item["query_offset"] < item["declaration_offset"] + item["declaration_bytes"] or
                item["type"] not in {"uint", "int", "float", "double", "bool", "other"} or
                type(item["exact_start"]) is not bool or type(item["compiler_used"]) is not bool or
                item["exact_start"] != (item["query_offset"] == item["declaration_offset"])):
            raise ReflectionError("reflection_output_semantics_rejected")
    # Rebuild a fixed export schema. Ignore arbitrary text/extra fields.
    return {"schema_version": 1, "status": result["status"],
            **{key: result[key] for key in ("constant_buffers", "bound_resources", "variables")},
            "query_matches": matches,
            "scope": "Compiler declarations only. Offset matches and compiler-used flags do not establish live bindings, full byte consumption, CPU mapping, frame association or camera semantics. No GPU execution."}


def checked_tools(helper, helper_sha256, library, library_sha256):
    helper, library = Path(helper).resolve(strict=True), Path(library).resolve(strict=True)
    if (helper.name != "odv_shader_reflection.exe" or library.name != "dxcompiler.dll" or
            not all(re.fullmatch(r"[0-9a-f]{64}", value) for value in (helper_sha256, library_sha256))):
        raise ReflectionError("unexpected_reflection_tools")
    if digest(helper, 8 * 1024 * 1024) != helper_sha256 or digest(library, 64 * 1024 * 1024) != library_sha256:
        raise ReflectionError("reflection_tool_fingerprint_mismatch")
    return helper, library


def reflect(container, helper, helper_sha256, library, library_sha256):
    if os.name != "nt":
        raise ReflectionError("reflection_requires_windows")
    parts = chunks(container)
    if b"DXIL" not in parts or b"STAT" not in parts:
        raise ReflectionError("reflection_program_or_metadata_missing")
    program_header(parts[b"DXIL"])
    program_header(parts[b"STAT"])
    helper, library = checked_tools(helper, helper_sha256, library, library_sha256)
    try:
        completed = subprocess.run([str(helper), str(library)], input=container,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                   timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
    except subprocess.TimeoutExpired:
        raise ReflectionError("reflection_helper_timeout") from None
    if completed.returncode != 0 or len(completed.stdout) > 128 * 1024:
        raise ReflectionError("reflection_helper_failed_or_output_oversized")
    try:
        result = validate(json.loads(completed.stdout))
    except (ValueError, UnicodeError, TypeError):
        raise ReflectionError("reflection_output_rejected") from None
    if digest(helper, 8 * 1024 * 1024) != helper_sha256 or digest(library, 64 * 1024 * 1024) != library_sha256:
        raise ReflectionError("reflection_tools_changed")
    return {**result, "helper_sha256": helper_sha256, "sdk_library_sha256": library_sha256}
