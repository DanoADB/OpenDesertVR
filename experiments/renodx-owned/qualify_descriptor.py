# SPDX-License-Identifier: MIT
"""Run only the pinned upstream owned descriptor regression in a fresh local case."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid

EXPECTED = {"layouts": 64, "threads_per_layout": 16, "successful_requests": 1024,
            "failed_requests": 0, "divergent_layouts": 0}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_result(data):
    if not 1 <= len(data) <= 4096:
        raise ValueError("owned_result_size")
    lines = data.decode("ascii", errors="strict").splitlines()
    if not lines or lines[0] != "PASS" or len(lines) != len(EXPECTED) + 1:
        raise ValueError("owned_result_marker_or_count")
    result = {}
    for line in lines[1:]:
        match = re.fullmatch(r"([a-z_]+)=(0|[1-9][0-9]{0,9})", line)
        if not match or match[1] in result:
            raise ValueError("owned_result_invalid_or_duplicate")
        result[match[1]] = int(match[2])
    if result != EXPECTED:
        raise ValueError("owned_result_counts_rejected")
    return result


def verify_sources(base, lock):
    base = base.resolve(strict=True)
    files = lock["files"]
    if not 1 <= len(files) <= 64 or len({item["path"] for item in files}) != len(files):
        raise ValueError("invalid_lock_files")
    for item in files:
        path = (base / item["path"]).resolve(strict=True)
        if not path.is_relative_to(base) or not re.fullmatch(r"[0-9a-f]{64}", item["sha256"]) or digest(path) != item["sha256"]:
            raise ValueError("source_or_license_changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--app-sha256", required=True)
    parser.add_argument("--addon-sha256", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    lock_path = Path(__file__).with_name("input-lock.json")
    lock = json.loads(lock_path.read_bytes())
    reference = root / "out/research" / lock["reference_directory"]
    case = root / "out/g1" / ("owned-descriptor-" + uuid.uuid4().hex)
    case.mkdir(parents=True, exist_ok=False)
    report = {"schema_version": 1, "evidence_kind": "owned_reshade_descriptor_cache_regression",
              "status": "FAILED", "stage": "PREFLIGHT", "controller_sha256": digest(Path(__file__)),
              "input_lock_sha256": digest(lock_path), "build_recipe_sha256": digest(Path(__file__).with_name("CMakeLists.txt")), "upstream_revision": lock["upstream_revision"],
              "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
              "configuration": args.configuration, "game_access": "NOT_RUN", "headset_test": "NOT_RUN_USER_DEFERRED",
              "full_devkit_execution": "NOT_RUN", "draw_binding_qualification": "NOT_RUN",
              "gpu_pixel_readback": "NOT_RUN", "camera_semantics": "UNVERIFIED", "native_stereo": "UNVERIFIED",
              "scope": "Only pinned external owned app/cache-test addon in a fresh workspace case. No installer or upstream deletion launcher, full Devkit, game, global MCP registration or headset. Loading/allocation result is not draw tracking, constant bytes or camera proof. Logs/binaries remain local and outside M0 packaging. File hashes do not attest all loaded code or prove absence of all external file I/O."}
    process = None
    code = 3
    try:
        if os.name != "nt" or report["source_dirty"] or any(not re.fullmatch(r"[0-9a-f]{64}", value)
                                                        for value in (args.app_sha256, args.addon_sha256)):
            raise ValueError("unqualified_platform_or_source")
        verify_sources(reference, lock)
        build = root / "out/build/renodx-owned-controlled" / args.configuration
        app = build / "descriptor_cache_race_app.exe"
        addon = build / "descriptor_cache_race_addon.addon64"
        dll = root / "out/tools/reshade-6.7.3/ReShade64.dll"
        binaries = [(app, "owned_app.exe", args.app_sha256),
                    (addon, addon.name, args.addon_sha256), (dll, "dxgi.dll", lock["reshade_dll_sha256"])]
        for source, name, expected in binaries:
            if digest(source) != expected:
                raise ValueError("binary_fingerprint_rejected")
            shutil.copyfile(source, case / name)
            if digest(case / name) != expected:
                raise ValueError("staging_changed_binary")
        report["binary_sha256"] = {name: expected for _, name, expected in binaries}
        for name in ("cache", "empty-effects", "empty-textures"):
            (case / name).mkdir()
        config = (f"[INSTALL]\nBasePath={case}\n[ADDON]\nAddonPath=.\n[GENERAL]\n"
                  f"NoReloadOnInit=1\nNoEffectCache=1\nIntermediateCachePath={case / 'cache'}\n"
                  f"EffectSearchPaths={case / 'empty-effects'}\nTextureSearchPaths={case / 'empty-textures'}\n")
        (case / "ReShade.ini").write_text(config, encoding="utf-8")
        report["config_before_sha256"] = digest(case / "ReShade.ini")
        env = os.environ.copy()
        env["RENODX_DESCRIPTOR_RACE_RESULT"] = str(case / "result.txt")
        report["stage"] = "OWNED_RUNNING"
        (case / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        start = time.monotonic()
        with (case / "stdout.log").open("xb") as stdout, (case / "stderr.log").open("xb") as stderr:
            process = subprocess.Popen([str(case / "owned_app.exe")], cwd=case, env=env,
                                       stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW)
            report["exit_code"] = process.wait(timeout=45)
        report["elapsed_ms"] = round((time.monotonic() - start) * 1000)
        report["stage"] = "RESULT_CHECK"
        result_path, log_path = case / "result.txt", case / "ReShade.log"
        if (report["exit_code"] != 0 or not result_path.is_file() or result_path.stat().st_size > 4096 or
                not log_path.is_file() or log_path.stat().st_size > 4 * 1024 * 1024):
            raise ValueError("owned_exit_or_reports_rejected")
        report["result_counts"] = parse_result(result_path.read_bytes())
        log = log_path.read_bytes()
        report.update(reshade_log_sha256=hashlib.sha256(log).hexdigest(), reshade_log_bytes=len(log),
                      reshade_loaded=b"Initializing crosire's ReShade" in log or b"Initialized." in log,
                      owned_addon_loaded=addon.name.encode() in log or b"RenoDX Descriptor Cache Race Test" in log)
        if not report["reshade_loaded"] or not report["owned_addon_loaded"]:
            raise ValueError("owned_loading_not_observed")
        report["stage"] = "POSTFLIGHT"
        verify_sources(reference, lock)
        if (digest(Path(__file__)) != report["controller_sha256"] or digest(lock_path) != report["input_lock_sha256"] or
                digest(Path(__file__).with_name("CMakeLists.txt")) != report["build_recipe_sha256"]):
            raise ValueError("controller_or_lock_changed")
        for source, name, expected in binaries:
            if digest(source) != expected or digest(case / name) != expected:
                raise ValueError("binary_postflight_changed")
        report.update(status="OBSERVED_OWNED_DESCRIPTOR_CACHE_REGRESSION", stage="COMPLETE", input_postflight="PASS")
        code = 0
    except (Exception, KeyboardInterrupt):
        report["failure_reason"] = "owned_input_loading_timeout_or_result_contract_rejected"
    finally:
        if process is not None and process.poll() is None:
            try:
                process.kill()  # Only the explicitly created owned child, never a game.
                process.wait(timeout=10)
                report["owned_child_terminated"] = True
            except Exception:
                report["owned_child_cleanup"] = "FAILED"
        (case / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        (root / "out/g1/current-owned-descriptor-case.json").write_text(json.dumps({"case_directory": str(case), "status": report["status"]}, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "stage": report["stage"], "case_directory": str(case)}))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
