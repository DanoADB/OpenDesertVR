# SPDX-License-Identifier: MIT
"""Compile/query original owned fixtures; no game files or GPU execution."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

directory = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(directory))
from reflection_query import digest, reflect
from shader_container import chunks


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    helper = Path(sys.argv[1]).resolve(strict=True)
    kits = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Windows Kits/10/bin"
    compilers = sorted(kits.glob("*/x64/dxc.exe"))
    check(bool(compilers), "Windows SDK DXC required for this qualification")
    compiler = compilers[-1]
    library = compiler.with_name("dxcompiler.dll")
    helper_hash, library_hash = digest(helper, 8 * 1024 * 1024), digest(library, 64 * 1024 * 1024)
    original = (directory / "owned_consumer.hlsl").read_text()
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)

        def compile_shader(source, target):
            hlsl, binary = root / "owned.hlsl", root / "owned.dxil"
            hlsl.write_text(source)
            run = subprocess.run([str(compiler), "-T", target, "-E", "main", "-Fo", str(binary), str(hlsl)],
                                 capture_output=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
            check(run.returncode == 0, "owned SDK compilation failed")
            result = binary.read_bytes()
            chunks(result)
            return result

        for token in ("uint", "float"):
            raw = compile_shader(original.replace("uint ownedFrame", token + " ownedFrame"), "cs_6_0")
            result = reflect(raw, helper, helper_hash, library, library_hash)
            frame = [item for item in result["query_matches"] if item["query_offset"] == 0x20]
            check(len(frame) == 1 and frame[0]["type"] == token and frame[0]["exact_start"] and
                  frame[0]["declaration_bytes"] == 4 and frame[0]["compiler_used"], "owned word type/usage differs")
            for offset, size in ((0x80, 12), (0x3e0, 64), (0x4e0, 64)):
                fields = [item for item in result["query_matches"] if item["query_offset"] == offset]
                check(len(fields) == 1 and fields[0]["exact_start"] and fields[0]["type"] == "float" and
                      fields[0]["declaration_bytes"] == size, "owned vector/matrix declaration differs")
            check("ownedFrame" not in json.dumps(result) and "ownedView" not in json.dumps(result),
                  "raw names exported")
        no_buffer = compile_shader("float4 main():SV_Target { return float4(1,0,0,1); }", "ps_6_0")
        result = reflect(no_buffer, helper, helper_hash, library, library_hash)
        check(result["constant_buffers"] == 0 and not result["query_matches"], "owned empty reflection misclassified")
        for raw in (b"DXBC" + bytes(28), b"DXBC" + bytes(1024 * 1024)):
            failed = subprocess.run([str(helper), str(library)], input=raw, capture_output=True, timeout=10,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            check(failed.returncode == 3 and json.loads(failed.stdout)["status"] == "REJECTED",
                  "native helper did not reject malformed/oversized input")
    print(json.dumps({"status": "PASS", "evidence_kind": "owned_sdk_reflection_qualification",
                      "fixture_queries": ["uint_word", "float_word", "vector", "matrices", "no_cbuffer"],
                      "helper_sha256": helper_hash, "sdk_library_sha256": library_hash,
                      "scope": "Original owned HLSL compiled and reflected only. No shader submitted to GPU or game data accessed."}))


if __name__ == "__main__":
    main()
