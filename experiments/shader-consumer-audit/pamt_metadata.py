# SPDX-License-Identifier: MIT
# Selected format layouts informed by CDMW-Full 0dfc3b1, MIT,
# Copyright (c) 2026 Ratrider. Full notice: docs/licenses/CDMW-MIT.txt.
"""Bounded metadata-only resolver, with no archive payload or filesystem API."""
from collections import Counter
import struct

from pamt_directory_probe import MAX_FILE, directory_markers


class Names:
    def __init__(self, block, compact):
        self.block, self.compact = block, compact

    def resolve(self, offset):
        parts, visited = [], set()
        while offset != 0xffffffff:
            if offset in visited or len(visited) >= 64 or not 0 <= offset < len(self.block):
                raise ValueError("invalid_name_parent_chain")
            visited.add(offset)
            cursor = offset
            if self.compact:
                distance = 0
                for shift in range(0, 35, 7):
                    if cursor >= len(self.block):
                        raise ValueError("truncated_name_parent")
                    value = self.block[cursor]
                    cursor += 1
                    distance |= (value & 127) << shift
                    if value < 128:
                        break
                else:
                    raise ValueError("oversized_name_parent")
                if distance > offset or distance > 0xffffffff:
                    raise ValueError("name_parent_before_block")
                parent = offset - distance if distance else 0xffffffff
            else:
                if cursor + 4 > len(self.block):
                    raise ValueError("truncated_name_parent")
                parent = struct.unpack_from("<I", self.block, cursor)[0]
                cursor += 4
            if cursor >= len(self.block):
                raise ValueError("truncated_name_length")
            length = self.block[cursor]
            cursor += 1
            if cursor + length > len(self.block):
                raise ValueError("truncated_name_text")
            parts.append(self.block[cursor:cursor + length].decode("utf-8", errors="strict"))
            if sum(map(len, parts)) > 4096:
                raise ValueError("oversized_name_path")
            offset = parent
        path = "".join(reversed(parts)).replace("\\", "/")
        if (path.startswith("/") or ":" in path or any(ord(char) < 32 for char in path) or
                any(part in {".", ".."} for part in path.split("/"))):
            raise ValueError("unsafe_virtual_name")
        return path.rstrip("/")


def resolve_metadata(data):
    directory_markers(data)  # Reuse strict bounded header/ID/directory checks.
    if len(data) > MAX_FILE:
        raise ValueError("index_size_outside_bounds")
    cursor = 12 + struct.unpack_from("<I", data, 4)[0] * 12

    def block(stride=1, maximum=MAX_FILE):
        nonlocal cursor
        if cursor + 4 > len(data):
            raise ValueError("truncated_metadata_count")
        count = struct.unpack_from("<I", data, cursor)[0]
        cursor += 4
        if count > maximum or cursor + count * stride > len(data):
            raise ValueError("metadata_block_outside_bounds")
        value = memoryview(data)[cursor:cursor + count * stride]
        cursor += count * stride
        return bytes(value), count

    directories, _ = block(maximum=8 * 1024 * 1024)
    filenames, _ = block(maximum=8 * 1024 * 1024)
    folders, folder_count = block(stride=16, maximum=100000)
    files, file_count = block(stride=20, maximum=500000)
    compact = cursor != len(data)
    extensions = None
    if compact:
        extension_bytes, _ = block(maximum=8192)
        if cursor != len(data) or not extension_bytes.startswith(b"\0"):
            raise ValueError("invalid_extension_table")
        extensions = extension_bytes.split(b"\0")
        if extensions[-1] == b"":
            extensions.pop()
        extensions = [value.decode("ascii", errors="strict") for value in extensions]
        if not 1 <= len(extensions) <= 256 or any(len(value) < 2 or not value.startswith(".") or "/" in value or
                "\\" in value or ":" in value or any(ord(char) < 32 for char in value) for value in extensions[1:]):
            raise ValueError("invalid_extension_table")
    folder_names, file_names = Names(directories, compact), Names(filenames, compact)
    ranges, targets, seen_paths = [], [], set()
    for _, name, start, count in struct.iter_unpack("<IIII", folders):
        path = folder_names.resolve(name)
        if path.casefold() in seen_paths or start + count > file_count:
            raise ValueError("duplicate_folder_or_invalid_range")
        seen_paths.add(path.casefold())
        if count:
            ranges.append((start, start + count))
        if path.casefold() == "shadercache__/d3d12":
            targets.append((start, start + count))
    ranges.sort()
    if any(left[1] > right[0] for left, right in zip(ranges, ranges[1:])):
        raise ValueError("overlapping_folder_ranges")
    if len(targets) != 1 or targets[0][1] - targets[0][0] > 100000:
        raise ValueError("shader_directory_not_unique_or_bounded")
    archive_count = struct.unpack_from("<I", data, 4)[0]
    archives = {struct.unpack_from("<I", data, 12 + i * 12)[0] for i in range(archive_count)}
    classes, flags, name_markers, duplicates = Counter(), Counter(), Counter(), set()
    minimum, maximum = None, 0
    selected = []  # Local metadata only; no path or record is exported by summary().
    for index, (name, offset, stored, original, archive, flag) in enumerate(struct.iter_unpack("<IIIIHH", files)):
        if archive not in archives:
            raise ValueError("invalid_entry_archive")
        if not targets[0][0] <= index < targets[0][1]:
            continue
        if not 1 <= stored <= 64 * 1024 * 1024 or not 1 <= original <= 64 * 1024 * 1024:
            raise ValueError("selected_entry_size_outside_bounds")
        path = file_names.resolve(name)
        if compact:
            if flag >> 8 >= len(extensions):
                raise ValueError("invalid_entry_extension")
            path += extensions[flag >> 8]
        if not path or path.casefold() in duplicates:
            raise ValueError("empty_or_duplicate_entry_name")
        duplicates.add(path.casefold())
        extension = path.rsplit(".", 1)[-1].casefold()
        role = extension if extension in {"padxil", "pars"} else "other"
        classes[role] += 1
        flags["encrypted" if (flag >> 4) & 15 else "unencrypted"] += 1
        flags["compressed" if stored != original else "uncompressed"] += 1
        for marker in ("scene", "camera", "view", "skinned"):
            name_markers[marker] += marker in path.casefold()
        minimum, maximum = min(minimum or stored, stored), max(maximum, stored)
        selected.append((path, archive, offset, stored, original, flag))
    return {"format": "compact" if compact else "legacy", "folder_count": folder_count,
            "file_count": file_count, "shader_directory_matches": 1, "selected_entries": len(selected),
            "extension_classes": dict(classes), "metadata_flags": dict(flags),
            "name_marker_counts": dict(name_markers), "minimum_stored_bytes": minimum,
            "maximum_stored_bytes": maximum, "_entries": selected}


def summary(metadata):
    return {key: value for key, value in metadata.items() if not key.startswith("_")}
