# SPDX-License-Identifier: MIT
"""Query the same explicitly fingerprinted shader via an isolated owned helper."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from inspect_shader_entry import run as inspect_entry
from reflection_query import ReflectionError, checked_tools, reflect
from shader_container import ContainerError


def run(args, report):
    report["stage"] = "REFLECTION_TOOL_PREFLIGHT"
    checked_tools(args.helper, args.helper_sha256, args.sdk_library, args.sdk_library_sha256)
    if not all(re.fullmatch(r"[0-9a-f]{64}", value) for value in
               (args.expected_payload_sha256, args.expected_container_sha256)):
        raise ReflectionError("invalid_expected_shader_fingerprints")
    report["stage"] = "PREFLIGHT"
    container = inspect_entry(args, report)
    report["status"] = "FAILED"
    report["stage"] = "REFLECTION_FINGERPRINTS"
    if (report["payload_sha256"] != args.expected_payload_sha256 or
            report["container"]["container_sha256"] != args.expected_container_sha256):
        raise ReflectionError("expected_shader_fingerprints_changed")
    report["stage"] = "REFLECTION"
    result = reflect(container, args.helper, args.helper_sha256, args.sdk_library, args.sdk_library_sha256)
    report.update(status="OBSERVED_SHADER_REFLECTION_METADATA", stage="COMPLETE", reflection=result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("game-root", "steam-manifest", "index", "helper", "sdk-library", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("index-sha256", "entry-name-sha256", "expected-payload-sha256", "expected-container-sha256",
                 "helper-sha256", "sdk-library-sha256"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    with args.output.open("x", encoding="utf-8") as output:
        report = {"schema_version": 1, "evidence_kind": "bounded_shader_reflection_metadata",
                  "status": "FAILED", "stage": "PREFLIGHT", "archive_payload_access": "NOT_RUN",
                  "shader_execution": "NOT_RUN", "camera_field_layout": "UNVERIFIED", "active_shader_binding": "UNVERIFIED",
                  "camera_publication": "UNVERIFIED", "native_stereo": "UNVERIFIED",
                  "source_sha256": {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() for name in
                                    ("query_shader_reflection.py", "reflection_query.py", "inspect_shader_entry.py", "shader_container.py",
                                     "resolve_shader_metadata.py", "pamt_metadata.py", "pamt_directory_probe.py")},
                  "scope": "Same index/entry and expected payload/container hashes; original range/build gates retained. One isolated SDK reflection helper, 10-second timeout plus cleanup, no alternate shader or retry. Typed declarations only; no names, IR, disassembly or raw payload exported. No GPU execution, process access, game write, shader compilation or camera/stereo claim. File/tool rechecks do not cryptographically attest loaded code or an atomic installation snapshot."}
        report["source_sha256"]["reflection/reflection.cpp"] = hashlib.sha256(
            (Path(__file__).parent / "reflection/reflection.cpp").read_bytes()).hexdigest()
        code = 3
        try:
            run(args, report)
            code = 0
        except Exception as error:
            report["status"] = "FAILED"
            report["failure_stage"] = report["stage"]
            report["failure_reason"] = str(error) if isinstance(error, (ReflectionError, ContainerError)) else "input_or_execution_rejected"
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")
    print(json.dumps(report, indent=2))
    return code


if __name__ == "__main__":
    sys.exit(main())
