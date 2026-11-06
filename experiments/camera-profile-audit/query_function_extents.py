# SPDX-License-Identifier: MIT
"""File-only function extents and linear decoding for three existing exact seeds."""
import argparse
import hashlib
import json
import mmap
import os
from pathlib import Path
import re
import sys

from audit_profile import Image
from function_extents import containing_function, disassemble, observe, runtime_functions, static_seeds

PROFILE_SHA256 = "b5cf5e46256f29380f269e81a78fcad0fd557b28a7c9d4e552ecb6a5a13d3365"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def identity(stat):
    return stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns


def run(args, report):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "frame-capture"))
    from game_build_gate import validate_game_build
    catalog = Path(__file__).resolve().parents[1] / "frame-capture/reviewed-builds.json"
    report["stage"] = "INPUT_PREFLIGHT"
    if (args.executable.name != "CrimsonDesert.exe" or not args.profile.is_file() or
            not 1 <= args.profile.stat().st_size <= 256 * 1024 or digest(args.profile) != PROFILE_SHA256 or
            args.dumpbin.name.casefold() != "dumpbin.exe" or not re.fullmatch("[0-9a-f]{64}", args.dumpbin_sha256) or
            digest(args.dumpbin) != args.dumpbin_sha256):
        raise ValueError("unreviewed_input_identity")
    expected = validate_game_build(args.executable, args.steam_manifest, catalog)
    catalog_hash = digest(catalog)
    report.update(expected, profile_sha256=PROFILE_SHA256, dumpbin_sha256=args.dumpbin_sha256,
                  reviewed_catalog_sha256=catalog_hash)
    profile = json.loads(args.profile.read_bytes())
    with args.executable.open("rb") as stream:
        before = identity(os.fstat(stream.fileno()))
        if not 1 <= before[2] <= 512 * 1024 * 1024:
            raise ValueError("executable_size_outside_bounds")
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
            if hashlib.sha256(data).hexdigest() != expected["executable_sha256"]:
                raise ValueError("mapped_executable_changed")
            image = Image(data)
            report["stage"] = "FUNCTION_METADATA"
            seeds = static_seeds(image, profile)
            report["function_extents"] = observe(image, seeds)
            records = runtime_functions(image)
            report["stage"] = "LINEAR_DECODING"
            report["decoded_seeds"] = []
            for role, seed in seeds:
                report["current_seed_role"] = role
                record = containing_function(records, seed)
                report["decoded_seeds"].append({"seed_role": role, **disassemble(
                    args.dumpbin, args.dumpbin_sha256, args.executable, image, record)})
            report["stage"] = "POSTFLIGHT"
            if hashlib.sha256(data).hexdigest() != expected["executable_sha256"]:
                raise ValueError("mapped_executable_changed_postflight")
        if before != identity(os.fstat(stream.fileno())) or before != identity(args.executable.stat()):
            raise ValueError("executable_file_identity_changed")
    if (validate_game_build(args.executable, args.steam_manifest, catalog) != expected or
            digest(catalog) != catalog_hash or digest(args.profile) != PROFILE_SHA256 or
            digest(args.dumpbin) != args.dumpbin_sha256):
        raise ValueError("input_postflight_changed")
    report.update(status="OBSERVED_STATIC_FUNCTION_EXTENTS_AND_LINEAR_DECODING", stage="COMPLETE",
                  input_postflight="PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("executable", "steam-manifest", "profile", "dumpbin", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--dumpbin-sha256", required=True)
    args = parser.parse_args()
    report = {"schema_version": 1, "evidence_kind": "bounded_static_seed_function_extents",
              "status": "FAILED", "stage": "PREFLIGHT", "input_postflight": "NOT_RUN",
              "source_sha256": {name: digest(Path(__file__).with_name(name)) for name in
                                ("query_function_extents.py", "function_extents.py", "audit_profile.py")},
              "game_execution": "NOT_RUN", "process_memory_access": "NOT_RUN", "game_write": "NOT_RUN",
              "producer_publication_contract": "UNVERIFIED", "camera_control": "UNVERIFIED",
              "camera_to_rendered_frame_association": "UNVERIFIED", "native_stereo": "UNVERIFIED",
              "scope": "Three unique prior static seeds only, bounded by x64 PE exception records. Installed explicit MSVC tool decodes at most 64 KiB per function with a ten-second call timeout. Output accepts at most 1 MiB per call; this is not a tool memory sandbox. Only sizes, code fingerprints and mnemonic counts retained, no bytes, addresses, operands, assembly or raw tool output. No cross-reference sweep, call-graph traversal, mod execution, process access, game launch, game writes or retry. Metadata extents and linear instruction decoding do not establish reachable instructions, all writers, publication ordering, camera semantics or a supported callback."}
    code = 3
    with args.output.open("x", encoding="utf-8") as output:
        try:
            run(args, report)
            code = 0
        except Exception:
            report["failure_reason"] = "input_function_metadata_or_tool_contract_rejected"
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")
    print(json.dumps(report, indent=2))
    return code


if __name__ == "__main__":
    sys.exit(main())
