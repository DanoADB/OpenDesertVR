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
rewrite_compiler = Path(sys.argv[2]).resolve(strict=True) if len(sys.argv) == 3 else compiler.parent
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
        hlsl.write_text(source)
        for entry, binary in (("vs", vs), ("ps", ps)):
            subprocess.run([str(compiler), "-T", entry + "_" + model, "-E", entry,
                            "-Fo", str(binary), str(hlsl)], check=True, capture_output=True,
                           timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
        hashes = [hashlib.sha256(p.read_bytes()).hexdigest() for p in (vs, ps)]
        for extra in ([], ["--stream"], ["--large"], ["--stream", "--large"],
                      ["--collapse"], ["--stream", "--collapse"]):
            run = subprocess.run([str(helper), str(rewrite_compiler), str(vs), str(ps)]+extra,
                                 capture_output=True, timeout=25, creationflags=subprocess.CREATE_NO_WINDOW)
            print(run.stdout.decode(errors="replace"), end="")
            if run.returncode:
                print(run.stderr.decode(errors="replace"), file=sys.stderr)
                raise RuntimeError("owned clip-shift GPU qualification failed: " + model)
        rejected = subprocess.run([str(helper), str(rewrite_compiler), str(ps), str(ps)],
                                  capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        if (rejected.returncode != 1 or b"unsupported_position_store" not in rejected.stderr or
                b"compile_failure_stage=4" not in rejected.stderr):
            raise RuntimeError("non-vertex shader was not rejected")
        if hashes != [hashlib.sha256(p.read_bytes()).hexdigest() for p in (vs, ps)]:
            raise RuntimeError("owned original shader files changed")
        # A missing row21.x must fail before recording any GPU work.
        rejected = subprocess.run([str(helper), str(rewrite_compiler), str(vs), str(ps), "--camera-field"],
                                  capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        if rejected.returncode != 1 or b"compile_failure_stage=4" not in rejected.stderr:
            raise RuntimeError("missing camera-field extraction was not rejected")
        field_source = source.replace("float4 camera;", "float4 camera[176];")
        # Keep this control inside the scalar SSA contract: a constant-array
        # lookup may compile to memory loads, which the value slice rejects.
        field_source = field_source.replace(
            "float2 p[4]={float2(-1,-1),float2(-1,1),float2(1,-1),float2(1,1)};",
            "float2 p=float2((id&2)?1:-1,(id&1)?1:-1);").replace("p[id]*w", "p*w")
        field_source = field_source.replace("+camera;", "+float4(camera[21].x,0,0,0);")
        field_source = field_source.replace("camera.y", "camera[0].y")
        hlsl.write_text(field_source)
        for entry, binary in (("vs", vs), ("ps", ps)):
            subprocess.run([str(compiler), "-T", entry + "_" + model, "-E", entry,
                            "-Fo", str(binary), str(hlsl)], check=True, capture_output=True,
                           timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
        field_hashes = [hashlib.sha256(p.read_bytes()).hexdigest() for p in (vs, ps)]
        for extra in (["--camera-field"], ["--stream", "--camera-field"]):
            run = subprocess.run([str(helper), str(rewrite_compiler), str(vs), str(ps)]+extra,
                                 capture_output=True, timeout=25, creationflags=subprocess.CREATE_NO_WINDOW)
            if run.returncode:
                raise RuntimeError("owned camera-field GPU qualification failed: " + model + ": " +
                                   run.stderr.decode(errors="replace"))
            print(run.stdout.decode(errors="replace"), end="")
            print("PASS: camera-field row21.x " + model + " " + ("stream" if "--stream" in extra else "legacy") +
                  "; identity/left/right/restoration pixels and original2816-byte input unchanged")
        if field_hashes != [hashlib.sha256(p.read_bytes()).hexdigest() for p in (vs, ps)]:
            raise RuntimeError("owned camera-field shader files changed")
        # A shared X/Y scalar is not the qualified X-only candidate.
        hlsl.write_text(field_source.replace("camera[21].x,0,0,0", "camera[21].x,camera[21].x,0,0"))
        subprocess.run([str(compiler), "-T", "vs_" + model, "-E", "vs", "-Fo", str(vs), str(hlsl)],
                       check=True, capture_output=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
        rejected = subprocess.run([str(helper), str(rewrite_compiler), str(vs), str(ps), "--camera-field"],
                                  capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        if rejected.returncode != 1 or b"compile_failure_stage=4" not in rejected.stderr:
            raise RuntimeError("shared X/Y camera-field dependency was not rejected")
print("PASS: owned SM6.0/6.6 clip and camera-field assembly/validation/GPU controls; game=NOT_RUN")
