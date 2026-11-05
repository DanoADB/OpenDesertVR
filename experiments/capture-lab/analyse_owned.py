# SPDX-License-Identifier: MIT
"""Replay only the owned clear fixture; this is not a game-camera analyser."""
import hashlib
import json
import os
from pathlib import Path
import sys

import renderdoc as rd


def flattened(actions):
    for action in actions:
        yield action
        yield from flattened(action.children)


def main():
    # Run through qrenderdoc --python: the app owns the embedded replay/Python runtime.
    config = json.loads(Path(os.environ["ODV_CAPTURE_LAB_CONFIG"]).read_text(encoding="utf-8-sig"))
    if config.get("fixture") != "owned_d3d12_clear":
        raise RuntimeError("This analyser accepts only the explicit owned-fixture configuration")
    capture_path = Path(config["capture"])
    output_path = Path(config["output"])
    if capture_path.resolve() == output_path.resolve():
        raise RuntimeError("Output must differ from the capture")
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        status = capture.OpenFile(str(capture_path), "", None)
        if status != rd.ResultCode.Succeeded:
            raise RuntimeError("Cannot open the owned fixture capture: " + str(status))
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        if status != rd.ResultCode.Succeeded or controller is None:
            raise RuntimeError("Cannot replay the owned fixture capture: " + str(status))
        if controller.GetAPIProperties().pipelineType != rd.GraphicsAPI.D3D12:
            raise RuntimeError("Owned capture is not D3D12")
        actions = list(flattened(controller.GetRootActions()))
        clears = [action for action in actions if action.flags & rd.ActionFlags.Clear]
        if not clears:
            raise RuntimeError("No GPU clear action found")
        controller.SetFrameEvent(actions[-1].eventId, True)
        expected_pixel = (0, 255, 255, 255)
        expected = bytes(expected_pixel) * (16 * 16)
        verified_targets = 0
        candidate_diagnostics = []
        for texture in controller.GetTextures():
            if texture.width == 16 and texture.height == 16 and texture.format.Name() == "R8G8B8A8_UNORM":
                data = bytes(controller.GetTextureData(texture.resourceId, rd.Subresource()))
                candidate_diagnostics.append({"format": texture.format.Name(), "bytes": len(data), "first_pixel": list(data[:4])})
                if data == expected:
                    verified_targets += 1
        if verified_targets != 1:
            raise RuntimeError("Owned target pixels did not match the known D3D12 clear: " + json.dumps(candidate_diagnostics))
        report = {
            "schema_version": 1,
            "evidence_kind": "owned_capture_tool_fixture",
            "capture_sha256": hashlib.sha256(capture_path.read_bytes()).hexdigest(),
            "api": str(controller.GetAPIProperties().pipelineType),
            "clear_actions": len(clears),
            "verified_rgba8_targets": verified_targets,
            "dimensions": [16, 16],
            "all_pixels_rgba8": list(expected_pixel),
            "status": "PASS",
            "game_capture": "NOT_RUN",
            "camera_control": "UNVERIFIED",
            "native_stereo": "UNVERIFIED",
        }
        output_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("Owned D3D12 capture replay and all 256 expected pixels verified.")
    finally:
        if controller is not None:
            controller.Shutdown()
        capture.Shutdown()


try:
    main()
except Exception as error:
    import traceback
    traceback.print_exc()
    try:
        config = json.loads(Path(os.environ["ODV_CAPTURE_LAB_CONFIG"]).read_text(encoding="utf-8-sig"))
        if config.get("fixture") == "owned_d3d12_clear" and Path(config["output"]).resolve() != Path(config["capture"]).resolve():
            Path(config["output"]).write_text(json.dumps({"status": "FAIL", "error_type": type(error).__name__, "reason": str(error)}, indent=2) + "\n", encoding="utf-8")
    except Exception:
        pass
finally:
    # The official qrenderdoc script entrypoint observes SystemExit and skips the UI.
    # Callers must verify the PASS report, not infer success from qrenderdoc's exit code.
    sys.exit()
