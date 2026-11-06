# SPDX-License-Identifier: MIT
"""Observe selected candidate fields as a bounded, read-only layout hypothesis."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from association import ObservationError
from camera_fields import observe_fields
from probe_association import PROFILE_HASH, digest, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--steam-manifest", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = {"schema_version": 1, "evidence_kind": "bounded_candidate_field_hypothesis",
              "status": "NOT_RUN", "stage": "PREFLIGHT", "process_memory_access": "NOT_RUN",
              "camera_field_layout": "UNVERIFIED", "runtime_camera_semantics": "UNVERIFIED",
              "camera_control": "UNVERIFIED", "native_stereo": "UNVERIFIED",
              "scope": "Published source-pointer and selected field offsets are hypotheses, not a relocated profile. No raw data, position, orientation, matrix, counter, address, PID or path is exported. File/path/module-size identity is checked; live image bytes are not hashed. Bracketed reads do not guarantee atomicity. No writes, hooks, debugger, privileges, scans, fallback or automatic retry."}

    def sampler(reader, global_rva, table_rva, image_size):
        with args.profile.open("rb") as profile_file:
            data = profile_file.read(256 * 1024 + 1)
        if len(data) > 256 * 1024 or hashlib.sha256(data).hexdigest() != PROFILE_HASH:
            raise ObservationError("unreviewed_field_hypothesis")
        camera = json.loads(data)["engineCamera"]
        if camera["layout"] != "renderer-camera-direct-v1":
            raise ObservationError("unreviewed_field_layout")
        return observe_fields(reader, global_rva, table_rva, image_size, camera["sourcePointerOffset"])

    exit_code = 3
    with args.output.open("x", encoding="utf-8") as output:
        try:
            report["source_sha256"] = {name: digest(Path(__file__).parent / name) for name in
                                        ["probe_fields.py", "camera_fields.py", "probe_association.py",
                                         "association.py", "audit_profile.py"]}
            run(args, report, sampler=sampler, result_key="candidate_fields")
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
