# SPDX-License-Identifier: MIT
"""Compile original owned shaders, then exercise private PSO geometry shifts."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

compiler = sorted((Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) /
                   "Windows Kits/10/bin").glob("*/x64/dxc.exe"))[-1]
helper = Path(sys.argv[1]).resolve(strict=True)
source = """
cbuffer Token:register(b0,space99){uint token;};
cbuffer Camera:register(b18,space35){float4 camera;};
float4 vs(uint id:SV_VertexID):SV_Position {
  float2 p[4]={float2(-1,-1),float2(-1,1),float2(1,-1),float2(1,1)};
  float w=token==2?2:1;
  return float4(p[id]*w,0.5*w,w)+camera;
}
float4 ps():SV_Target{return float4(1,camera.y,0,1);}
"""
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    hlsl, vs, ps = root / "owned.hlsl", root / "owned.vs.cso", root / "owned.ps.cso"
    hlsl.write_text(source)
    for model in ("6_0", "6_6"):
        for entry, binary in (("vs", vs), ("ps", ps)):
            subprocess.run([str(compiler), "-T", entry + "_" + model, "-E", entry,
                            "-Fo", str(binary), str(hlsl)], check=True, capture_output=True,
                           timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
        hashes = [hashlib.sha256(p.read_bytes()).hexdigest() for p in (vs, ps)]
        for extra in ([], ["--stream"], ["--large"], ["--stream", "--large"],
                      ["--collapse"], ["--stream", "--collapse"]):
            run = subprocess.run([str(helper), str(compiler.parent), str(vs), str(ps)]+extra,
                                 capture_output=True, timeout=25, creationflags=subprocess.CREATE_NO_WINDOW)
            print(run.stdout.decode(errors="replace"), end="")
            if run.returncode:
                print(run.stderr.decode(errors="replace"), file=sys.stderr)
                raise RuntimeError("owned clip-shift GPU qualification failed: " + model)
        rejected = subprocess.run([str(helper), str(compiler.parent), str(ps), str(ps)],
                                  capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        if (rejected.returncode != 1 or b"unsupported_position_store" not in rejected.stderr or
                b"compile_failure_stage=4" not in rejected.stderr):
            raise RuntimeError("non-vertex shader was not rejected")
        if hashes != [hashlib.sha256(p.read_bytes()).hexdigest() for p in (vs, ps)]:
            raise RuntimeError("owned original shader files changed")
print("PASS: owned SDK SM6.0/6.6 assembly, validation, private-PSO geometry and non-VS rejection; game=NOT_RUN")
