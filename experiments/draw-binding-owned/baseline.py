# SPDX-License-Identifier: MIT
"""Run real WARP pixel controls without a capture tool, game or headset."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import uuid
import zlib


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    executable = Path(sys.argv[1]).resolve(strict=True)
    root = Path(sys.argv[2]).resolve()
    root.mkdir(parents=True, exist_ok=True)
    case = root / ("owned-draw-controls-" + uuid.uuid4().hex)
    case.mkdir()
    before = digest(executable)
    results = []
    shaders = []
    for submitted, color in ((3, "black"), (4, "yellow")):
        output = case / color
        output.mkdir()
        args = [str(executable), str(output)]
        if submitted == 4:
            args.append("--submit-fourth")
        run = subprocess.run(args, capture_output=True, timeout=20, check=False,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        (output / "stdout.txt").write_bytes(run.stdout)
        (output / "stderr.txt").write_bytes(run.stderr)
        if run.returncode:
            raise RuntimeError(f"owned_fixture_exit_{run.returncode}; evidence retained at {output}")
        expected = {"schemaVersion": 1, "status": "PASS", "adapter": "WARP", "frames": 2,
                    "pixelsVerifiedPerFrame": 2048, "recordedDrawsPerFrame": 4,
                    "submittedDrawsPerFrame": submitted, "fourthColumn": color,
                    "captureTool": "NOT_RUN", "headset": "NOT_RUN", "game": "NOT_RUN"}
        actual = json.loads((output / "result.json").read_text(encoding="utf-8"))
        if (actual != expected or any(type(actual[key]) is not type(value)
                                      for key, value in expected.items())):
            raise ValueError("unexpected_owned_pixel_result")
        pair = []
        for name in ("owned.vs.cso", "owned.ps.cso"):
            data = (output / name).read_bytes()
            if not 32 <= len(data) <= 65536 or data[:4] != b"DXBC":
                raise ValueError("unexpected_owned_shader")
            pair.append({"name": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                         "crc32": f"0x{zlib.crc32(data):08X}"})
        shaders.append(pair)
        results.append(actual)
    if shaders[0] != shaders[1] or before != digest(executable):
        raise ValueError("owned_binary_or_shader_identity_changed")
    report = {"schemaVersion": 1, "status": "PASS", "appSha256": before,
              "shaders": shaders[0], "controls": results,
              "scope": "Actual software-GPU pixels only; capture bindings and execution metadata NOT_RUN"}
    (case / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    print(f"Private case retained: {case}")


if __name__ == "__main__":
    main()
