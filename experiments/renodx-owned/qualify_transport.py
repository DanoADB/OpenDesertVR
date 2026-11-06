# SPDX-License-Identifier: MIT
"""Qualify the private Devkit transport on the original clear-only owned app."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import traceback
import uuid

from bridge_client import Bridge


def digest(path):
    if not path.is_file() or not 1 <= path.stat().st_size <= 32 * 1024 * 1024:
        raise ValueError("input_size_outside_bounds")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_lock(reference, lock):
    base = reference.resolve(strict=True)
    files = lock["files"]
    if not 1 <= len(files) <= 256 or len({x["path"] for x in files}) != len(files):
        raise ValueError("invalid_source_lock")
    for item in files:
        path = (base / item["path"]).resolve(strict=True)
        if not path.is_relative_to(base) or digest(path) != item["sha256"]:
            raise ValueError("source_lock_mismatch")


def verify_pipe(target, pipe):
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.WaitNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD]
    api.WaitNamedPipeW.restype = wintypes.BOOL
    api.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                               wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    api.CreateFileW.restype = wintypes.HANDLE
    api.GetNamedPipeServerProcessId.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.ULONG)]
    api.GetNamedPipeServerProcessId.restype = wintypes.BOOL
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    api.CloseHandle.restype = wintypes.BOOL
    deadline = time.monotonic() + 8
    path = "\\\\.\\pipe\\" + pipe
    while time.monotonic() < deadline:
        if target.poll() is not None:
            raise ValueError("owned_target_exited_before_pipe")
        if api.WaitNamedPipeW(path, 100):
            handle = api.CreateFileW(path, 0xc0000000, 0, None, 3, 0, None)
            if handle not in (None, ctypes.c_void_p(-1).value):
                try:
                    pid = wintypes.ULONG()
                    if not api.GetNamedPipeServerProcessId(handle, ctypes.byref(pid)) or pid.value != target.pid:
                        raise ValueError("pipe_server_is_not_owned_child")
                    return
                finally:
                    if not api.CloseHandle(handle):
                        raise ValueError("pipe_probe_handle_close_failed")
        time.sleep(0.05)
    raise ValueError("owned_pipe_readiness_timeout")


def eligible_device(status):
    devices = status.get("devices")
    if (type(devices) is not list or not 1 <= len(devices) <= 16 or
            type(status.get("deviceCount")) is not int or status["deviceCount"] != len(devices)):
        raise ValueError("invalid_device_summary")
    eligible = []
    for device in devices:
        if type(device) is not dict:
            raise ValueError("invalid_device_record")
        primary = device.get("primarySwapchain", {})
        if type(primary) is not dict:
            raise ValueError("invalid_swapchain_record")
        if (device.get("api") == "d3d12" and type(device.get("swapchains")) is int and device["swapchains"] > 0 and
                type(primary.get("width")) is int and primary["width"] == 320 and
                type(primary.get("height")) is int and primary["height"] == 180):
            if type(device.get("index")) is not int or not 0 <= device["index"] <= 15:
                raise ValueError("invalid_device_index")
            eligible.append(device)
    if len(eligible) != 1:
        raise ValueError("owned_presenting_device_not_unique")
    return eligible[0]


def summary_counts(summary, index):
    if (type(summary.get("deviceIndex")) is not int or summary["deviceIndex"] != index or
            summary.get("snapshotActive") is not False or summary.get("snapshotQueued") is not False):
        raise ValueError("snapshot_summary_identity_or_state")
    result = {}
    for name in ("capturedDraws", "trackedShaders", "resourceUsageEntries", "shaderUsageEntries", "snapshotRows"):
        value = summary.get(name)
        if type(value) is not int or not 0 <= value <= 10000:
            raise ValueError("snapshot_summary_count_rejected")
        result[name] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("app-sha256", "addon-sha256", "bridge-sha256"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    lock_path = Path(__file__).with_name("devkit-input-lock.json")
    lock = json.loads(lock_path.read_bytes())
    case = root / "out/g1" / ("owned-devkit-transport-" + uuid.uuid4().hex)
    case.mkdir(parents=True, exist_ok=False)
    report = {"schema_version": 1, "evidence_kind": "owned_devkit_transport_qualification", "status": "FAILED",
              "stage": "PREFLIGHT", "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
              "source_sha256": {name: digest(Path(__file__).with_name(name)) for name in
                                ("qualify_transport.py", "bridge_client.py", "devkit-input-lock.json")},
              "game_access": "NOT_RUN", "headset_test": "NOT_RUN_USER_DEFERRED", "draw_binding_qualification": "NOT_RUN",
              "camera_semantics": "UNVERIFIED", "input_postflight": "NOT_RUN",
              "scope": "Clear-only owned app and private pinned Devkit/stdio bridge, not game instrumentation. Exact pipe server PID must equal the created owned child. Observer selection/snapshot state changes are requested; shader text/dumps, replacements and resource mutation tools are excluded. No global MCP registration or game-folder writes. Metadata transport is not draw tracking, per-draw bytes, GPU completion or camera proof. Raw local metadata/logs remain private; file hashes are not full loaded-code or external-I/O attestation."}
    target = client = None
    streams = []
    code = 3
    try:
        if os.name != "nt" or report["source_dirty"] or any(not re.fullmatch(r"[0-9a-f]{64}", value)
                                                        for value in (args.app_sha256, args.addon_sha256, args.bridge_sha256)):
            raise ValueError("unqualified_platform_or_source")
        reference = root / "out/research" / lock["reference_directory"]
        verify_lock(reference, lock)
        app = root / "out/build/present-fixture-ci/Release/odv_present_fixture.exe"
        tools = root / "out/build/renodx-devkit-owned/Release"
        binaries = [(app, "owned_app.exe", args.app_sha256),
                    (tools / "renodx-devkit.addon64", "renodx-devkit.addon64", args.addon_sha256),
                    (tools / "renodx-mcp-bridge.exe", "renodx-mcp-bridge.exe", args.bridge_sha256),
                    (root / "out/tools/reshade-6.7.3/ReShade64.dll", "dxgi.dll",
                     "ec9245d05c11751f2ac0d2256e6921ad8fb36be9172ef6d587856591eb729a25")]
        for source, name, expected in binaries:
            if digest(source) != expected:
                raise ValueError("binary_fingerprint_mismatch")
            shutil.copyfile(source, case / name)
        report["binary_sha256"] = {name: expected for _, name, expected in binaries}
        for directory in ("cache", "empty-effects", "empty-textures", "devkit/live", "devkit/boot", "devkit/dump"):
            (case / directory).mkdir(parents=True, exist_ok=True)
        config = (f"[INSTALL]\nBasePath={case}\n[ADDON]\nAddonPath=.\n[GENERAL]\nNoReloadOnInit=1\nNoEffectCache=1\n"
                  f"IntermediateCachePath={case / 'cache'}\nEffectSearchPaths={case / 'empty-effects'}\nTextureSearchPaths={case / 'empty-textures'}\n"
                  f"[renodx-dev]\nLivePath={case / 'devkit/live'}\n"
                  "TraceAll=0\nTracePipelineCreation=0\nTraceDescriptorTables=0\nTraceConstantBuffers=0\nSnapshotTraceWithSnapshot=0\nSwapChainDeviceProxyMode=0\n")
        (case / "ReShade.ini").write_text(config, encoding="utf-8")
        report["config_before_sha256"] = digest(case / "ReShade.ini")
        for name in ("target.stdout", "target.stderr", "bridge.stderr"):
            streams.append((case / name).open("xb"))
        report["stage"] = "OWNED_START"
        (case / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        target = subprocess.Popen([str(case / "owned_app.exe"), str(case / "capture"), "--hidden"], cwd=case,
                                  stdout=streams[0], stderr=streams[1], creationflags=subprocess.CREATE_NO_WINDOW)
        pipe = "renodx-devkit-mcp-" + str(target.pid)
        report["stage"] = "PIPE_IDENTITY"
        verify_pipe(target, pipe)
        report["pipe_server_matches_owned_child"] = True
        for connection in range(2):
            report["stage"] = "BRIDGE_CONNECTION_" + str(connection + 1)
            client = Bridge(case / "renodx-mcp-bridge.exe", pipe, streams[2])
            client.initialize()
            status = client.tool("devkit_status", {})
            if status.get("serverInfo", {}).get("version") != "bc0ead3-owned-qualification":
                raise ValueError("unexpected_devkit_build")
            device = eligible_device(status)
            index = device["index"]
            if connection == 0:
                report["stage"] = "SNAPSHOT_REQUEST"
                client.tool("devkit_select_device", {"deviceIndex": index})
                client.tool("devkit_queue_snapshot", {"deviceIndex": index})
                complete = False
                for _ in range(12):
                    time.sleep(0.05)
                    observed = client.tool("devkit_status", {})
                    snapshot = observed.get("snapshot", {})
                    if snapshot.get("active") is False and snapshot.get("queued") is False:
                        complete = True
                        break
                if not complete:
                    raise ValueError("owned_snapshot_not_complete")
                summary = client.tool("devkit_snapshot_summary", {"deviceIndex": index})
                report["snapshot_counts"] = summary_counts(summary, index)
                (case / "snapshot-summary.private.json").write_text(json.dumps(summary, indent=2) + "\n")
                report["snapshot_observed_complete"] = True
                report["owned_device_count"] = status["deviceCount"]
                report["presenting_api"] = device["api"]
            else:
                report["fresh_bridge_reconnect"] = True
            if client.close() != 0:
                raise ValueError("bridge_exit_failed")
            client = None
            if target.poll() is not None:
                raise ValueError("owned_target_exited_before_stop")
            verify_pipe(target, pipe)
        report["stage"] = "COOPERATIVE_STOP"
        (case / "stop.flag").write_text("owned transport qualification complete\n")
        report["owned_exit_code"] = target.wait(timeout=10)
        if report["owned_exit_code"] != 0:
            raise ValueError("owned_target_exit_failed")
        report["stage"] = "POSTFLIGHT"
        verify_lock(reference, lock)
        for name, expected in report["source_sha256"].items():
            if digest(Path(__file__).with_name(name)) != expected:
                raise ValueError("controller_changed")
        for source, name, expected in binaries:
            if digest(source) != expected or digest(case / name) != expected:
                raise ValueError("binary_changed")
        report["devkit_asset_files"] = sum(1 for path in (case / "devkit").rglob("*") if path.is_file())
        if report["devkit_asset_files"]:
            raise ValueError("unexpected_boot_live_or_dump_files")
        report.update(status="OBSERVED_OWNED_DEVKIT_TRANSPORT", stage="COMPLETE", input_postflight="PASS")
        code = 0
    except (Exception, KeyboardInterrupt):
        report["failure_reason"] = "owned_identity_transport_snapshot_or_cleanup_contract_rejected"
        (case / "failure.private.txt").write_text(traceback.format_exc())
    finally:
        (case / "stop.flag").touch(exist_ok=True)
        if client is not None:
            try:
                client.close()
            except Exception:
                report["bridge_cleanup"] = "FAILED"
        if target is not None and target.poll() is None:
            try:
                target.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    target.kill()  # Only this explicitly created owned target.
                    target.wait(timeout=5)
                    report["owned_target_terminated"] = True
                except Exception:
                    report["owned_target_cleanup"] = "FAILED"
        for stream in streams:
            stream.close()
        (case / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        (root / "out/g1/current-owned-devkit-transport.json").write_text(json.dumps({"case_directory": str(case), "status": report["status"]}, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "stage": report["stage"], "case_directory": str(case)}))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
