# SPDX-License-Identifier: MIT
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "summary", Path(__file__).parents[1] / "scripts" / "summarize-presentmon.py")
SUMMARY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SUMMARY)


class PresentationSummaryTests(unittest.TestCase):
    def summarize(self, rows, displayed_column=True):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.csv"
            with path.open("w", newline="", encoding="utf-8-sig") as stream:
                writer = csv.writer(stream)
                columns = ["Application", "ProcessID", "SwapChainAddress", "PresentRuntime",
                           "PresentMode", "MsBetweenPresents"]
                if displayed_column:
                    columns.append("DisplayedTime")
                writer.writerow(columns)
                writer.writerows(rows if displayed_column else [row[:-1] for row in rows])
            return SUMMARY.summarize(path, 12345)

    def row(self, interval, address="private-address-A", pid=12345, app="CrimsonDesert.exe"):
        return [app, pid, address, "DXGI", "Composed: Flip", interval, "NA"]

    def test_separates_streams_and_filters_process(self):
        report = self.summarize([
            self.row(10), self.row(20), self.row(100, "private-address-B"),
            self.row(999, pid=54321), self.row(999, app="Other.exe")])
        self.assertEqual(report["matching_rows"], 3)
        self.assertEqual([s["between_presents"]["median_ms"] for s in report["streams"]], [15, 100])
        self.assertEqual(report["streams"][0]["between_presents"]["p95_ms"], 20)
        exported = json.dumps(report)
        for secret in ["private-address", "12345", "54321", "Other.exe"]:
            self.assertNotIn(secret, exported)
        self.assertEqual(report["d3d12_render_path"], "UNVERIFIED")
        self.assertEqual(report["native_stereo"], "UNVERIFIED")

    def test_missing_and_zero_are_not_timings(self):
        report = self.summarize([self.row("NA"), self.row(""), self.row(0), self.row(14)])
        stream = report["streams"][0]
        self.assertEqual(stream["missing_intervals"], 2)
        self.assertEqual(stream["zero_intervals_excluded"], 1)
        self.assertEqual(stream["between_presents"]["samples"], 1)

    def test_missing_only_is_not_measured(self):
        stream = self.summarize([self.row("NA")])["streams"][0]
        self.assertEqual(stream["between_presents"]["status"], "NOT_RUN")

    def test_missing_display_column_is_not_zero_dropped(self):
        stream = self.summarize([self.row(16.7)], displayed_column=False)["streams"][0]
        self.assertIsNone(stream["not_displayed_rows"])
        self.assertEqual(stream["not_displayed_rows_status"], "NOT_RUN")

    def test_display_column_na_is_counted(self):
        stream = self.summarize([self.row(16.7)])["streams"][0]
        self.assertEqual(stream["not_displayed_rows"], 1)
        self.assertEqual(stream["not_displayed_rows_status"], "OBSERVED")

    def test_bad_timings_fail(self):
        for value in ["NaN", "inf", "-inf", "-1", "garbage"]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.summarize([self.row(value)])

    def test_no_matching_process_fails(self):
        with self.assertRaises(ValueError):
            self.summarize([self.row(10, pid=54321)])

    def test_no_application_paths_exported(self):
        report = self.summarize([self.row(10, app=r"C:\PrivateUser\CrimsonDesert.exe")])
        self.assertNotIn("PrivateUser", json.dumps(report))

    def test_arbitrary_runtime_and_mode_not_exported(self):
        row = self.row(10)
        row[3] = "private-runtime-path"
        row[4] = "private-mode-path"
        report = self.summarize([row])
        self.assertEqual(report["streams"][0]["present_runtimes"], ["Unknown"])
        self.assertNotIn("private-", json.dumps(report))

    def test_missing_swapchain_fails(self):
        with self.assertRaises(ValueError):
            self.summarize([self.row(10, address="")])


if __name__ == "__main__":
    unittest.main()
