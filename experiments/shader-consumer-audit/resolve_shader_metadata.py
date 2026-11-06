# SPDX-License-Identifier: MIT
"""Resolve one hash-pinned shader-directory candidate; no PAZ payload access."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from pamt_directory_probe import read_index
from pamt_metadata import resolve_metadata, summary


def run(args):
    root = args.game_root.resolve(strict=True)
    index = args.index
    # Exact one installed metadata file; no upstream discovery/import code.
    if (index.is_symlink() or index.parent.is_symlink() or not re.fullmatch(r"[0-9]{4}", index.parent.name) or
            index.name != "0.pamt" or index.parent.parent.resolve(strict=True) != root or
            not re.fullmatch(r"[0-9a-f]{64}", args.index_sha256)):
        raise ValueError("unexpected_index_identity")
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "frame-capture"))
    from game_build_gate import validate_game_build
    catalog = Path(__file__).resolve().parents[1] / "frame-capture/reviewed-builds.json"
    catalog_hash = hashlib.sha256(catalog.read_bytes()).hexdigest()
    identity = validate_game_build(root / "bin64/CrimsonDesert.exe", args.steam_manifest, catalog)
    data, digest = read_index(index)
    if digest != args.index_sha256:
        raise ValueError("index_fingerprint_mismatch")
    result = summary(resolve_metadata(data))
    if read_index(index)[1] != digest or validate_game_build(
            root / "bin64/CrimsonDesert.exe", args.steam_manifest, catalog) != identity or hashlib.sha256(
                catalog.read_bytes()).hexdigest() != catalog_hash:
        raise ValueError("inputs_changed")
    return {**identity, "reviewed_catalog_sha256": catalog_hash, "index_sha256": digest,
            "index_bytes": len(data), "status": "OBSERVED_SHADER_DIRECTORY_METADATA", "metadata": result,
            "scope": "One resolved metadata directory and its entry records. Flag meanings follow pinned public source, not payload validation. No raw metadata/names/locations exported, PAZ payload access, decryption, extraction, process access, game write or retry. Active shader bindings and CPU/GPU camera mapping remain unverified."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=Path, required=True)
    parser.add_argument("--steam-manifest", type=Path, required=True)
    parser.add_argument("--index", type=Path, required=True)
    parser.add_argument("--index-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with args.output.open("x", encoding="utf-8") as output:
        report = {"schema_version": 1, "evidence_kind": "bounded_shader_directory_metadata",
                  "status": "FAILED", "camera_publication": "UNVERIFIED", "native_stereo": "UNVERIFIED",
                  "source_sha256": {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                                    for name in ("resolve_shader_metadata.py", "pamt_metadata.py", "pamt_directory_probe.py")}}
        code = 3
        try:
            report.update(run(args))
            code = 0
        except Exception:
            report["failure_reason"] = "input_identity_metadata_or_bounds_rejected"
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")
    print(json.dumps(report, indent=2))
    return code


if __name__ == "__main__":
    sys.exit(main())
