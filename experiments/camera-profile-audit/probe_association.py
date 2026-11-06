# SPDX-License-Identifier: MIT
"""Scoped read-only observation of a reviewed game process's pointer association."""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct
import sys

from association import ObservationError, ReadOnlyProcess, observe_process
from audit_profile import Image, observe

PROFILE_HASH = "b5cf5e46256f29380f269e81a78fcad0fd557b28a7c9d4e552ecb6a5a13d3365"


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def candidates(data, profile):
    observation = observe(data, profile)
    if (observation["reference_relationship"]["status"] != "OBSERVED" or
            observation["vtable_relationship"]["status"] != "OBSERVED_UNIQUE"):
        raise ObservationError("offline_relationships_not_unique_and_valid")
    image = Image(data)
    camera = profile["engineCamera"]
    reference = image.matches(camera["cameraReferencePattern"])[0]
    global_rva = reference + 7 + struct.unpack("<i", image.read(reference + 3, 4))[0]
    fingerprints = camera["cameraVtableFingerprints"]
    functions = [image.matches(item["pattern"]) for item in fingerprints]
    tables, reason = image.vtable_candidates(fingerprints, functions)
    if reason or len(tables) != 1:
        raise ObservationError("offline_table_not_unique")
    return global_rva, tables[0], image.image_size


def run(args, report, sampler=observe_process, result_key="association"):
    executable = args.executable.resolve()
    if executable.name.casefold() != "crimsondesert.exe":
        raise ObservationError("unexpected_executable_name")
    if args.profile.stat().st_size > 256 * 1024 or digest(args.profile) != PROFILE_HASH:
        raise ObservationError("unreviewed_profile")
    profile = json.loads(args.profile.read_bytes())
    gate_directory = Path(__file__).resolve().parents[1] / "frame-capture"
    sys.path.insert(0, str(gate_directory))
    from game_build_gate import validate_game_build
    catalog = gate_directory / "reviewed-builds.json"
    identity = validate_game_build(executable, args.steam_manifest, catalog)
    report.update(identity)
    report["profile_sha256"] = PROFILE_HASH
    report["reviewed_catalog_sha256"] = digest(catalog)
    report["stage"] = "OFFLINE_RELATIONSHIPS"
    if not 1 <= executable.stat().st_size <= 512 * 1024 * 1024:
        raise ObservationError("executable_size_outside_bounds")
    with executable.open("rb") as stream:
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
            if hashlib.sha256(data).hexdigest() != identity["executable_sha256"]:
                raise ObservationError("executable_changed")
            global_rva, table_rva, image_size = candidates(data, profile)
            if hashlib.sha256(data).hexdigest() != identity["executable_sha256"]:
                raise ObservationError("executable_changed")
    report["stage"] = "PROCESS_IDENTITY"
    report["process_memory_access"] = "ATTEMPTED"
    with ReadOnlyProcess(args.pid, executable) as reader:
        report["stage"] = "SAMPLING"
        result = sampler(reader, global_rva, table_rva, image_size)
        report["stage"] = "POSTFLIGHT"
        reader.active()
        if validate_game_build(executable, args.steam_manifest, catalog) != identity or digest(args.profile) != PROFILE_HASH:
            raise ObservationError("inputs_changed_during_observation")
        reader.active()
    report[result_key] = result
    report["status"] = "OBSERVED"
    report["stage"] = "COMPLETE"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--steam-manifest", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = {"schema_version": 1, "evidence_kind": "bounded_readonly_pointer_association",
              "status": "NOT_RUN", "stage": "PREFLIGHT", "process_memory_access": "NOT_RUN",
              "runtime_camera_semantics": "UNVERIFIED", "camera_field_layout": "UNVERIFIED",
              "camera_control": "UNVERIFIED", "native_stereo": "UNVERIFIED",
              "scope": "Only a global/object first-pointer association. File identity/path/module size are checked; the loaded image is not cryptographically hashed. Bracketed reads do not guarantee atomicity. No game launch, hooks, writes, debugger, privilege adjustment, field offsets or automatic retry."}
    exit_code = 3
    # Reserve the output before process access; never overwrite prior evidence.
    with args.output.open("x", encoding="utf-8") as output:
        try:
            report["source_sha256"] = {name: digest(Path(__file__).parent / name)
                                        for name in ["probe_association.py", "association.py", "audit_profile.py"]}
            run(args, report)
            exit_code = 0
        except Exception as error:
            report["failure_stage"] = report["stage"]
            report["status"] = "FAILED"
            report["failure_reason"] = str(error) if isinstance(error, ObservationError) else "preflight_or_execution_rejected"
            report["failure_type"] = type(error).__name__
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")
    print(json.dumps(report, indent=2))
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
