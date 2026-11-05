# SPDX-License-Identifier: MIT
"""Controlled early-start capture through the external tool's supported Python API."""
import hashlib
import json
import os
from pathlib import Path
import sys
import time
import traceback

import renderdoc as rd


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def save(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def capture_metadata(path):
    capture = rd.OpenCaptureFile()
    try:
        status = capture.OpenFile(str(path), "", None)
        if status != rd.ResultCode.Succeeded or capture.DriverName() != "D3D12":
            raise RuntimeError("Expected a readable D3D12 capture")
        return {"capture_sha256": digest(path), "capture_bytes": path.stat().st_size, "api": "D3D12"}
    finally:
        capture.Shutdown()


def run(config, report, report_path):
    mode = config["mode"]
    executable = Path(config["executable"]).resolve()
    case = report_path.parent.resolve()
    expected_name = {"owned_launch": "odv_capture_fixture.exe", "owned_control": "odv_present_fixture.exe",
                     "owned_control_resume": "odv_present_fixture.exe", "game_title": "crimsondesert.exe",
                     "game_title_resume": "crimsondesert.exe"}.get(mode)
    if expected_name is None or executable.name.casefold() != expected_name:
        raise RuntimeError("Unexpected mode or executable identity")
    if digest(executable) != config["executable_sha256"]:
        raise RuntimeError("Executable fingerprint changed before launch")
    timeout = int(config.get("timeout_seconds", 180))
    if not 1 <= timeout <= 300:
        raise RuntimeError("Capture timeout must be between 1 and 300 seconds")
    prefix = case / "raw"
    if list(case.glob("*.rdc")):
        raise RuntimeError("Use a fresh capture directory")
    resume = mode.endswith("_resume")
    source_case = case
    prior_context = None
    if resume:
        source_case = Path(config["source_case"]).resolve()
        if source_case == case or source_case.parent != case.parent:
            raise RuntimeError("Resume requires a distinct sibling case from this experiment")
        prior_config = json.loads((source_case / "local-config.json").read_text(encoding="utf-8-sig"))
        prior_report = json.loads((source_case / "capture-info.json").read_text(encoding="utf-8-sig"))
        prior_context = json.loads((source_case / "local-context.json").read_text(encoding="utf-8-sig"))
        if (prior_config["mode"] != mode[:-len("_resume")] or
                Path(prior_config["executable"]).resolve() != executable or
                prior_config["executable_sha256"] != config["executable_sha256"] or
                prior_report["status"] != "FAILED" or
                prior_report.get("failure_stage") not in {"READY_FOR_FRAME", "CAPTURE_REQUESTED"}):
            raise RuntimeError("Resume origin is not a failed capture on the same reviewed executable")
        report["resume_origin_report_sha256"] = digest(source_case / "capture-info.json")
    old_paths = {path.resolve() for path in source_case.glob("*.rdc")}
    options = rd.CaptureOptions()
    options.hookIntoChildren = False
    options.allowVSync = True
    options.allowFullscreen = True
    options.apiValidation = False
    options.captureCallstacks = False
    # One literal Windows path argument; the final 'raw' component cannot end in '\\'.
    if '"' in str(prefix):
        raise RuntimeError("Invalid capture-prefix path")
    arguments = '"' + str(prefix) + '"' if mode in {"owned_launch", "owned_control"} else ""
    launch_context = config.get("launch_context", "executable_directory")
    working_directory = executable.parent
    if resume:
        launch_context = "existing_early_layer"
    elif launch_context == "steam_development_appid":
        if mode != "game_title" or (case / "steam_appid.txt").read_text(encoding="utf-8") != "3321460":
            raise RuntimeError("Expected the reviewed game App ID in the isolated case directory")
        working_directory = case
    elif launch_context != "executable_directory":
        raise RuntimeError("Unreviewed launch context")
    report["launch_context"] = launch_context
    report.update({"status": "LAUNCHING", "mode": mode, "executable_sha256": config["executable_sha256"]})
    save(report_path, report)
    if resume:
        ident = prior_context["target_ident"]
        report["status"] = "CONNECTING_EXISTING_LAYER"
        save(report_path, report)
    else:
        result = rd.ExecuteAndInject(str(executable), str(working_directory), arguments, [], str(prefix), options,
                                     mode == "owned_launch")
        if result.result != rd.ResultCode.Succeeded:
            raise RuntimeError("External capture launch failed: " + str(result.result))
        ident = result.ident
    if mode == "owned_launch":
        path = case / "raw_capture.rdc"
        report.update(capture_metadata(path))
        report["status"] = "OWNED_LAUNCH_VALIDATED"
        save(report_path, report)
        return
    if not ident:
        raise RuntimeError("No target-control endpoint returned")
    target = rd.CreateTargetControl("localhost", ident, "OpenDesertVR G1", False)
    if target is None:
        raise RuntimeError("Cannot connect to the newly launched local target")
    try:
        name = target.GetTarget().replace("\\", "/").rsplit("/", 1)[-1].casefold()
        if name not in {expected_name, Path(expected_name).stem} or target.GetPID() <= 0:
            raise RuntimeError("Target-control identity does not match the launched executable")
        if resume and target.GetPID() != prior_context["target_process_id"]:
            raise RuntimeError("The early-loaded target process has changed")
        save(case / "local-context.json", {
            "target_process_id": target.GetPID(), "target_ident": ident,
            "privacy": "LOCAL ONLY: do not publish this PID/connection context or raw capture",
        })
        deadline = time.monotonic() + timeout
        requested = False
        report["status"] = "WAITING_FOR_D3D12"
        save(report_path, report)
        while time.monotonic() < deadline:
            if not target.Connected():
                raise RuntimeError("Capture target disconnected; no automatic relaunch or child injection")
            was_requested = requested
            message = target.ReceiveMessage(None)
            if not requested and "D3D12" in target.GetAPI().upper():
                if report["status"] != "READY_FOR_FRAME":
                    report["status"] = "READY_FOR_FRAME"
                    save(report_path, report)
                # Operator/agent verifies the title menu first. No keyboard/screenshot hotkey.
                if (case / "capture-now.flag").is_file():
                    target.TriggerCapture(1)
                    requested = True
                    report["status"] = "CAPTURE_REQUESTED"
                    save(report_path, report)
            if was_requested and message.type == rd.TargetControlMessageType.NewCapture:
                entry = message.newCapture
                path = Path(entry.path).resolve()
                if path in old_paths:
                    continue
                if not entry.local or path.parent != source_case or path.suffix.casefold() != ".rdc":
                    raise RuntimeError("Capture is not a local file inside the selected case directory")
                if digest(executable) != config["executable_sha256"]:
                    raise RuntimeError("Game executable fingerprint changed during capture")
                report["status"] = "SAVING_CAPTURE"
                save(report_path, report)
                # Keep an independent local snapshot even if the tool later cleans its original.
                source_hash = digest(path)
                snapshot = case / "frame.rdc"
                with path.open("rb") as source, snapshot.open("xb") as destination:
                    for block in iter(lambda: source.read(1024 * 1024), b""):
                        destination.write(block)
                metadata = capture_metadata(snapshot)
                if metadata["capture_sha256"] != source_hash:
                    raise RuntimeError("Saved capture does not match the completed source")
                report.update(metadata)
                is_game = mode in {"game_title", "game_title_resume"}
                report.update({"status": "CAPTURED" if is_game else "OWNED_CONTROL_CAPTURED",
                               "game_gpu_capture": "OBSERVED" if is_game else "NOT_RUN"})
                save(report_path, report)
                return
            time.sleep(0.1)
        raise RuntimeError("Capture deadline expired; no capture success inferred")
    finally:
        target.Shutdown()


report_path = None
report = {"schema_version": 1, "evidence_kind": "controlled_external_frame_capture",
          "status": "NOT_STARTED", "game_gpu_capture": "NOT_RUN",
          "world_render_pass_identity": "UNVERIFIED", "camera_control": "UNVERIFIED",
          "native_stereo": "UNVERIFIED"}
try:
    config_path = Path(os.environ["ODV_FRAME_CAPTURE_CONFIG"])
    config = json.loads(config_path.read_text(encoding="utf-8-sig"))
    report_path = config_path.parent / "capture-info.json"
    report["script_sha256"] = digest(Path(config["script"]))
    run(config, report, report_path)
except Exception as error:
    traceback.print_exc()
    if report_path is not None:
        (report_path.parent / "local-error.txt").write_text(traceback.format_exc(), encoding="utf-8")
        report.update({"failure_stage": report["status"], "status": "FAILED",
                       "failure_type": type(error).__name__})
        save(report_path, report)
finally:
    # qrenderdoc owns replay lifetime. Its process exit alone does not prove success.
    sys.exit()
