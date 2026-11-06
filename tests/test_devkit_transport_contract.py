# SPDX-License-Identifier: MIT
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "experiments/renodx-owned"))
from qualify_transport import eligible_device, summary_counts


class DevkitTransportContractTests(unittest.TestCase):
    def test_owned_presenting_device_must_be_unique_and_typed(self):
        device = {"index": 0, "api": "d3d12", "swapchains": 1, "primarySwapchain": {"width": 320, "height": 180}}
        self.assertEqual(eligible_device({"deviceCount": 1, "devices": [device]})["index"], 0)
        for status in ({"deviceCount": 2, "devices": [device, device]},
                       {"deviceCount": True, "devices": [device]},
                       {"deviceCount": 1, "devices": [{**device, "index": True}]},
                       {"deviceCount": 1, "devices": [{**device, "api": "d3d11"}]}):
            with self.assertRaises(ValueError):
                eligible_device(status)

    def test_only_completed_selected_snapshot_counts_are_exported(self):
        value = {"deviceIndex": 0, "snapshotActive": False, "snapshotQueued": False,
                 "capturedDraws": 0, "trackedShaders": 0, "resourceUsageEntries": 0,
                 "shaderUsageEntries": 0, "snapshotRows": 0, "privateText": "not exported"}
        self.assertNotIn("privateText", summary_counts(value, 0))
        for bad in ({**value, "deviceIndex": 1}, {**value, "snapshotActive": True},
                    {**value, "capturedDraws": True}, {**value, "snapshotRows": 10001}):
            with self.assertRaises(ValueError):
                summary_counts(bad, 0)


if __name__ == "__main__":
    unittest.main()
