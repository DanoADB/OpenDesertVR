# SPDX-License-Identifier: MIT
# Selected PAMT header layout informed by CDMW-Full 0dfc3b1, MIT,
# Copyright (c) 2026 Ratrider. See docs/licenses/CDMW-MIT.txt.
"""Locate shader-directory marker candidates in bounded read-only PAMT metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

MAX_FILE = 16 * 1024 * 1024
MAX_TOTAL = 128 * 1024 * 1024
MAX_DIRECTORY = 8 * 1024 * 1024
MARKERS = (b"shadercache__", b"d3d12")


def directory_markers(data):
    if not 16 <= len(data) <= MAX_FILE:
        raise ValueError("index_size_outside_bounds")
    _, count, _ = struct.unpack_from("<III", data)
    start = 12 + count * 12
    if not 1 <= count <= 65536 or start + 4 > len(data):
        raise ValueError("archive_table_outside_bounds")
    ids = [struct.unpack_from("<I", data, 12 + i * 12)[0] for i in range(count)]
    if len(set(ids)) != count or any(value > 65535 for value in ids):
        raise ValueError("archive_identifiers_invalid")
    size = struct.unpack_from("<I", data, start)[0]
    start += 4
    if size > MAX_DIRECTORY or start + size > len(data):
        raise ValueError("directory_block_outside_bounds")
    block = data[start:start + size]
    return {"directory_bytes": size,
            "marker_counts": {marker.decode("ascii"): block.count(marker) for marker in MARKERS}}


def indices(root):
    packages = sorted(path for path in root.iterdir() if re.fullmatch(r"[0-9]{4}", path.name))
    if not 1 <= len(packages) <= 64:
        raise ValueError("package_count_outside_bounds")
    result = []
    for package in packages:
        index = package / "0.pamt"
        if package.is_symlink() or not package.is_dir() or index.is_symlink() or not index.is_file():
            raise ValueError("unexpected_package_or_index")
        result.append(index)
    sizes = [path.stat().st_size for path in result]
    if any(not 16 <= size <= MAX_FILE for size in sizes):
        raise ValueError("index_size_outside_bounds")
    if sum(sizes) > MAX_TOTAL:
        raise ValueError("aggregate_index_bytes_outside_bounds")
    return result


def read_index(path, limit=MAX_FILE):
    maximum = min(MAX_FILE, limit)
    if not 16 <= path.stat().st_size <= maximum:
        raise ValueError("index_size_outside_bounds")
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    if not 16 <= len(data) <= maximum:
        raise ValueError("index_size_outside_bounds")
    return data, hashlib.sha256(data).hexdigest()


def run(root, manifest):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "frame-capture"))
    from game_build_gate import validate_game_build
    catalog = Path(__file__).resolve().parents[1] / "frame-capture/reviewed-builds.json"
    catalog_digest = hashlib.sha256(catalog.read_bytes()).hexdigest()
    root = root.resolve(strict=True)
    identity = validate_game_build(root / "bin64/CrimsonDesert.exe", manifest, catalog)
    paths = indices(root)
    observations = []
    total = 0
    for path in paths:
        data, digest = read_index(path, MAX_TOTAL - total)
        total += len(data)
        observations.append({"index_sha256": digest, "index_bytes": len(data), **directory_markers(data)})
    # Exact bytes hashed again; no stored input payload or private path in report.
    if indices(root) != paths:
        raise ValueError("indices_changed_during_observation")
    total_after = 0
    for path, item in zip(paths, observations):
        data, digest = read_index(path, MAX_TOTAL - total_after)
        total_after += len(data)
        if digest != item["index_sha256"]:
            raise ValueError("indices_changed_during_observation")
    if (validate_game_build(root / "bin64/CrimsonDesert.exe", manifest, catalog) != identity or
            hashlib.sha256(catalog.read_bytes()).hexdigest() != catalog_digest):
        raise ValueError("game_identity_changed")
    return {**identity, "reviewed_catalog_sha256": catalog_digest,
            "status": "OBSERVED_DIRECTORY_MARKERS", "index_count": len(paths),
            "index_bytes_per_pass": total, "index_passes": 2, "observations": observations,
            "scope": "Header/directory bytes only parsed; rest hashed for identity. Marker counts are candidate locators, not resolved paths, shader entry identities, payload/reflection data, active bindings or camera semantics. No archive payload, process memory, game write, decryption, extraction, import of upstream code or retry."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=Path, required=True)
    parser.add_argument("--steam-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with args.output.open("x", encoding="utf-8") as output:
        report = {"schema_version": 1, "evidence_kind": "bounded_pamt_directory_markers",
                  "status": "FAILED", "camera_publication": "UNVERIFIED", "native_stereo": "UNVERIFIED",
                  "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
        exit_code = 3
        try:
            report.update(run(args.game_root, args.steam_manifest))
            exit_code = 0
        except Exception:
            # Exception text can contain input paths; deliberately omit it.
            report["failure_reason"] = "input_identity_structure_or_bounds_rejected"
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")
    print(json.dumps(report, indent=2))
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
