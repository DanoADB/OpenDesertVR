# SPDX-License-Identifier: MIT
"""Read structural metadata from one explicitly selected plain shader entry."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys

from pamt_directory_probe import read_index
from pamt_metadata import resolve_metadata
from resolve_shader_metadata import checked_location
from shader_container import ContainerError, MAX_SHADER, parse_pasc


def file_identity(value):
    return value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns


def read_payload(index, entry, report):
    _, archive, offset, stored, original, flags = entry
    if flags & 255 or stored != original or not 1 <= stored <= MAX_SHADER:
        raise ValueError("entry_not_plain_uncompressed_and_bounded")
    path = index.with_name(f"{archive}.paz")
    if path.is_symlink() or not path.is_file():
        raise ValueError("unexpected_archive_path")
    report["stage"] = "PAYLOAD"
    report["archive_payload_access"] = "ATTEMPTED"
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or offset < 0 or offset + stored > before.st_size:
            raise ValueError("entry_range_outside_archive")
        stream.seek(offset)
        payload = stream.read(stored)
        stream.seek(offset)
        again = stream.read(stored)
        if (len(payload) != stored or again != payload or
                file_identity(os.fstat(stream.fileno())) != file_identity(before) or
                file_identity(path.stat()) != file_identity(before)):
            raise ValueError("archive_range_changed_or_short_read")
    report.update(archive_payload_access="OBSERVED", archive_bytes_read=stored * 2,
                  payload_bytes=stored, payload_sha256=hashlib.sha256(payload).hexdigest())
    return payload


def run(args, report):
    root, index = checked_location(args)
    if not re.fullmatch(r"[0-9a-f]{64}", args.entry_name_sha256):
        raise ValueError("invalid_entry_selector")
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "frame-capture"))
    from game_build_gate import validate_game_build
    catalog = Path(__file__).resolve().parents[1] / "frame-capture/reviewed-builds.json"
    catalog_hash = hashlib.sha256(catalog.read_bytes()).hexdigest()
    identity = validate_game_build(root / "bin64/CrimsonDesert.exe", args.steam_manifest, catalog)
    report.update(identity, reviewed_catalog_sha256=catalog_hash)
    report["stage"] = "INDEX"
    data, digest = read_index(index)
    if digest != args.index_sha256:
        raise ValueError("index_fingerprint_mismatch")
    entries = resolve_metadata(data)["_entries"]
    matches = [entry for entry in entries if hashlib.sha256(entry[0].encode("utf-8")).hexdigest() == args.entry_name_sha256]
    if len(matches) != 1 or not matches[0][0].casefold().endswith(".padxil"):
        raise ValueError("shader_entry_not_unique")
    report.update(index_sha256=digest, index_bytes=len(data), entry_name_sha256=args.entry_name_sha256)
    payload = read_payload(index, matches[0], report)
    report["stage"] = "POSTFLIGHT"
    if (read_index(index)[1] != digest or validate_game_build(
            root / "bin64/CrimsonDesert.exe", args.steam_manifest, catalog) != identity or
            hashlib.sha256(catalog.read_bytes()).hexdigest() != catalog_hash):
        raise ValueError("inputs_changed")
    report["input_postflight"] = "PASS"
    report["stage"] = "CONTAINER"
    _, description = parse_pasc(payload)
    report.update(status="OBSERVED_SINGLE_SHADER_CONTAINER", stage="COMPLETE", container=description)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=Path, required=True)
    parser.add_argument("--steam-manifest", type=Path, required=True)
    parser.add_argument("--index", type=Path, required=True)
    parser.add_argument("--index-sha256", required=True)
    parser.add_argument("--entry-name-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with args.output.open("x", encoding="utf-8") as output:
        report = {"schema_version": 1, "evidence_kind": "bounded_single_shader_container",
                  "status": "FAILED", "stage": "PREFLIGHT", "archive_payload_access": "NOT_RUN",
                  "shader_execution": "NOT_RUN", "camera_field_layout": "UNVERIFIED",
                  "active_shader_binding": "UNVERIFIED", "camera_publication": "UNVERIFIED", "native_stereo": "UNVERIFIED",
                  "source_sha256": {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() for name in
                                    ("inspect_shader_entry.py", "shader_container.py", "resolve_shader_metadata.py",
                                     "pamt_metadata.py", "pamt_directory_probe.py")},
                  "scope": "One hash-selected metadata record and two reads of its exact declared unencrypted/uncompressed range. No decoder, decryption, payload export, upstream program, compiler/IR execution, process access, game write, shader scan, fallback or retry. The game-build gate does not authenticate archive contents. File/range rechecks are not an atomic installation snapshot."}
        code = 3
        try:
            run(args, report)
            code = 0
        except Exception as error:
            report["failure_reason"] = str(error) if isinstance(error, ContainerError) else "input_identity_range_or_execution_rejected"
            report["failure_stage"] = report["stage"]
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")
    print(json.dumps(report, indent=2))
    return code


if __name__ == "__main__":
    sys.exit(main())
