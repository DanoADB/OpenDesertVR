# SPDX-License-Identifier: MIT
"""Local D3D12 replay with aggregate structure only; no shader/resource payload export."""
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import sys
import traceback

import renderdoc as rd


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def actions_in(roots):
    for action in roots:
        yield action
        yield from actions_in(action.children)


def main(config):
    capture_path = Path(config["capture"]).resolve()
    output = Path(config["output"]).resolve()
    if capture_path == output or output.exists():
        raise RuntimeError("Use a fresh summary output distinct from the capture")
    capture_hash = digest(capture_path)
    if capture_hash != config["capture_sha256"]:
        raise RuntimeError("Capture fingerprint differs from the reviewed local record")
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        if capture.OpenFile(str(capture_path), "", None) != rd.ResultCode.Succeeded:
            raise RuntimeError("Cannot open the capture")
        if capture.DriverName() != "D3D12":
            raise RuntimeError("Expected a D3D12 capture")
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        if status != rd.ResultCode.Succeeded or controller is None:
            raise RuntimeError("D3D12 replay did not initialize")
        if controller.GetAPIProperties().pipelineType != rd.GraphicsAPI.D3D12:
            raise RuntimeError("Replay API differs from the capture record")
        actions = list(actions_in(controller.GetRootActions()))
        counts = {}
        for name in ("Drawcall", "Dispatch", "Clear", "Copy", "Present"):
            flag = getattr(rd.ActionFlags, name)
            counts[name] = sum(bool(action.flags & flag) for action in actions)
        texture_shapes = Counter((int(t.width), int(t.height), int(t.depth), int(t.mips),
                                  int(t.arraysize), int(t.msSamp)) for t in controller.GetTextures())
        buffer_sizes = Counter(int(b.length) for b in controller.GetBuffers())
        report = {
            "schema_version": 1, "evidence_kind": "aggregate_d3d12_frame_structure",
            "status": "REPLAYED", "capture_sha256": capture_hash,
            "script_sha256": digest(Path(config["script"])), "api": "D3D12",
            "action_count": len(actions), "action_flags": counts,
            "texture_shape_counts": [
                {"width": s[0], "height": s[1], "depth": s[2], "mips": s[3],
                 "array_size": s[4], "samples": s[5], "count": n}
                for s, n in sorted(texture_shapes.items())],
            "buffer_size_counts": [{"bytes": size, "count": n} for size, n in sorted(buffer_sizes.items())],
            "world_render_pass_identity": "UNVERIFIED", "camera_control": "UNVERIFIED",
            "native_stereo": "UNVERIFIED",
            "scope": "Aggregate replay structure only. No names, resource IDs, shaders, buffer/texture payloads or addresses exported.",
        }
        # Replay success does not assert fidelity or even a world-rendering pass.
        with output.open("x", encoding="utf-8") as destination:
            destination.write(json.dumps(report, indent=2, allow_nan=False) + "\n")
    finally:
        if controller is not None:
            controller.Shutdown()
        capture.Shutdown()


config_path = None
try:
    config_path = Path(os.environ["ODV_FRAME_SUMMARY_CONFIG"])
    main(json.loads(config_path.read_text(encoding="utf-8-sig")))
except Exception:
    # Tool exception text can contain local/game metadata; keep it in local stdout only.
    traceback.print_exc()
    if config_path is not None:
        try:
            with (config_path.parent / "summary-local-error.txt").open("x", encoding="utf-8") as error_file:
                error_file.write(traceback.format_exc())
        except Exception:
            pass
finally:
    sys.exit()
