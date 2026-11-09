# SPDX-License-Identifier: MIT
"""Own-geometry GPU perturbation and exact restoration, no game or headset."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import uuid


app = Path(sys.argv[1]).resolve(strict=True)
root = Path(sys.argv[2]).resolve()
root.mkdir(parents=True, exist_ok=True)
case = root / ("scalar-render-" + uuid.uuid4().hex)
case.mkdir()
before = hashlib.sha256(app.read_bytes()).hexdigest()
run = subprocess.run([str(app), str(case)], capture_output=True, timeout=20,
                     creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
(case / "stdout.txt").write_bytes(run.stdout)
(case / "stderr.txt").write_bytes(run.stderr)
if run.returncode == 77:
    print("NOT_RUN: enhanced barriers unsupported")
    sys.exit(77)
if run.returncode:
    raise RuntimeError(f"scalar_render_exit_{run.returncode}; retained evidence at {case}")
actual = json.loads((case / "result.json").read_text())
expected = {"schemaVersion": 1, "status": "PASS", "adapter": "WARP", "frames": 2,
            "pixelsVerifiedPerFrame": 2048, "recordedDrawsPerFrame": 5,
            "submittedDrawsPerFrame": 4, "fourthColumn": "black",
            "captureTool": "NOT_RUN", "headset": "NOT_RUN", "game": "NOT_RUN"}
if actual != expected or before != hashlib.sha256(app.read_bytes()).hexdigest():
    raise RuntimeError("scalar_render_result_or_binary_mismatch")
print(run.stdout.decode(errors="replace").strip())
print(f"Retained owned evidence: {case}")
