# SPDX-License-Identifier: MIT
"""Summarize one game's PresentMon CSV without exporting IDs or addresses."""

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


def timing(values):
    if not values:
        return {"status": "NOT_RUN", "samples": 0}
    ordered = sorted(values)
    return {
        "status": "OBSERVED",
        "samples": len(values),
        "median_ms": statistics.median(ordered),
        "p95_ms": ordered[math.ceil(len(ordered) * 0.95) - 1],
        "max_ms": ordered[-1],
    }


def summarize(path, process_id):
    required = {"Application", "ProcessID", "SwapChainAddress", "PresentRuntime",
                "PresentMode", "MsBetweenPresents"}
    chains = {}
    matching = 0
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("CSV lacks required PresentMon presentation columns")
        for row in reader:
            # The capture may contain unrelated rows. Never merge them into the game.
            application = (row.get("Application") or "").replace("\\", "/").rsplit("/", 1)[-1]
            if application.casefold() != "crimsondesert.exe":
                continue
            try:
                row_process_id = int(row["ProcessID"])
            except (ValueError, TypeError):
                raise ValueError("Invalid game process ID") from None
            if row_process_id != process_id:
                continue
            if None in row or any(row.get(key) is None for key in required):
                raise ValueError("Malformed game CSV row")
            address = row["SwapChainAddress"]
            if not address:
                raise ValueError("Missing swapchain identity")
            chain = chains.setdefault(address, {
                "rows": 0, "intervals": [], "missing_intervals": 0,
                "zero_intervals": 0, "runtimes": set(), "modes": set(),
                "not_displayed_rows": 0,
            })
            chain["rows"] += 1
            matching += 1
            value = row["MsBetweenPresents"].strip()
            if value in ("", "NA", "N/A"):
                chain["missing_intervals"] += 1
            else:
                try:
                    interval = float(value)
                except ValueError:
                    raise ValueError("Invalid presentation interval") from None
                if not math.isfinite(interval) or interval < 0:
                    raise ValueError("Non-finite or negative presentation interval")
                if interval == 0:
                    chain["zero_intervals"] += 1
                else:
                    chain["intervals"].append(interval)
            runtime = row["PresentRuntime"]
            chain["runtimes"].add(runtime if runtime in {"DXGI", "D3D9", "Other"} else "Unknown")
            mode = row["PresentMode"]
            modes = {
                "Hardware: Legacy Flip", "Hardware: Legacy Copy to front buffer",
                "Hardware: Independent Flip", "Composed: Flip",
                "Hardware Composed: Independent Flip", "Composed: Copy with GPU GDI",
                "Composed: Copy with CPU GDI",
            }
            chain["modes"].add(mode if mode in modes else "Unknown")
            if row.get("DisplayedTime", "").strip() == "NA":
                chain["not_displayed_rows"] += 1
    if not matching:
        raise ValueError("No rows matched the requested Crimson Desert process")
    result = []
    for index, chain in enumerate(chains.values(), 1):
        result.append({
            "stream": index,
            "rows": chain["rows"],
            "present_runtimes": sorted(chain["runtimes"]),
            "present_modes": sorted(chain["modes"]),
            "between_presents": timing(chain["intervals"]),
            "missing_intervals": chain["missing_intervals"],
            "zero_intervals_excluded": chain["zero_intervals"],
            "not_displayed_rows": chain["not_displayed_rows"],
        })
    return {
        "schema_version": 1,
        "evidence_kind": "external_presentmon_csv_summary",
        "application": "CrimsonDesert.exe",
        "input_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "matching_rows": matching,
        "streams": result,
        "d3d12_render_path": "UNVERIFIED",
        "camera_control": "UNVERIFIED",
        "native_stereo": "UNVERIFIED",
        "limitations": [
            "Intervals describe separate presentation streams, not measured headset latency.",
            "DXGI presentation does not distinguish D3D11 from D3D12 or prove stereo.",
            "DisplayedTime=NA counts only rows marked not displayed by PresentMon; it is not an XR compositor drop metric.",
            "No scene, game settings or workload is inferred from the CSV.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--process-id", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.process_id <= 0:
        parser.error("process ID must be positive")
    if args.csv.resolve() == args.output.resolve():
        parser.error("output must differ from input CSV")
    try:
        report = summarize(args.csv, args.process_id)
    except (OSError, UnicodeError, csv.Error, ValueError) as error:
        parser.exit(2, f"Capture summary failed: {error}\n")
    # A failed parse never overwrites an earlier evidence summary.
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
