# SPDX-License-Identifier: MIT
"""Qualify the narrow observer on SDK-produced original owned shader text."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

compiler = sorted((Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) /
                   "Windows Kits/10/bin").glob("*/x64/dxc.exe"))[-1]
helper = Path(sys.argv[1]).resolve(strict=True)
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    for target in ("vs_6_0", "vs_6_6"):
        for dynamic in (False, True):
            source, binary, text = root / "owned.hlsl", root / "owned.cso", root / "owned.txt"
            source.write_text("cbuffer Owned:register(b18,space35){float4 words[176];};\n"
                              "float4 main(uint id:SV_VertexID):SV_Position{return words[" +
                              ("id%176" if dynamic else "175") + "];}")
            subprocess.run([str(compiler), "-T", target, "-E", "main", "-Fo", str(binary),
                            "-Fc", str(text), str(source)], check=True, capture_output=True, timeout=20,
                           creationflags=subprocess.CREATE_NO_WINDOW)
            result = subprocess.run([str(helper), str(text)], capture_output=True, timeout=10,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            expected = 3 if dynamic else 0
            if result.returncode != expected:
                raise RuntimeError(f"owned SDK {target} dynamic={dynamic}: {result.stdout.decode()}")
print("PASS: actual SDK SM6.0/6.6 constant last-row reads accepted; dynamic reads rejected; game=NOT_RUN")
