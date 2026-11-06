# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import struct
import sys
import unittest

directory = Path(__file__).resolve().parents[1] / "experiments/camera-profile-audit"
sys.path.insert(0, str(directory))
import camera_fields as fields
from association import ObservationError


def buffer(frame=5):
    data = bytearray(0xb00)

    def floats(offset, values):
        struct.pack_into("<" + "f" * len(values), data, offset, *values)

    struct.pack_into("<I", data, 0x20, frame)
    floats(0x30, (1600, 1200, 1 / 1600, 1 / 1200))
    floats(0x80, (10, 20, 30))
    floats(0x90, (0, 0, 1))
    identity = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    floats(0x3e0, identity[:12] + (-10, -20, -30, 1))
    floats(0x420, identity)
    floats(0x4e0, (1.125, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 1, 0, 0, .2, 0))
    floats(0x860, (.2,))
    floats(0xac0, (6360000,))
    return data


def selected(data):
    return {offset: bytes(data[offset:offset + size]) for offset, size in fields.RANGES}


class FieldTests(unittest.TestCase):
    def test_owned_camera_geometry_and_projection_are_consistent(self):
        frame, state = fields.decode(selected(buffer()))
        self.assertEqual(frame, 5)
        self.assertEqual(state[:3], (10, 20, 30))

    def test_nonfinite_basis_resolution_translation_and_projection_rejected(self):
        for offset, value in [(0x80, float("nan")), (0x3e0, 2), (0x30, 0), (0x38, 1),
                               (0x410, 99), (0x450, 1), (0x4e0 + 11 * 4, 0),
                               (0x4e0 + 8 * 4, .5), (0x860, 0), (0xac0, 1)]:
            data = buffer()
            struct.pack_into("<f", data, offset, value)
            with self.assertRaises(ObservationError):
                fields.decode(selected(data))

    def test_reflected_basis_and_wrong_selected_size_rejected(self):
        data = buffer()
        struct.pack_into("<f", data, 0x98, -1)
        with self.assertRaises(ObservationError):
            fields.decode(selected(data))
        data = selected(buffer())
        data[0x80] = data[0x80][:-1]
        with self.assertRaises(ObservationError):
            fields.decode(data)

    def sample(self, frames, torn=False, source_change=False):
        data = buffer(frames[0])
        counts = {"bytes": 0, "calls": 0}
        sample = 0

        def pointer(address):
            counts["bytes"] += 8
            return {100: 200, 200: 300, 200 + 1072: 4000 + (8 if source_change and counts["calls"] else 0)}[address]

        def read(address, size):
            counts["bytes"] += size
            counts["calls"] += 1
            result = bytes(data[address - 4000:address - 4000 + size])
            if torn and counts["calls"] == len(fields.RANGES):
                struct.pack_into("<I", data, 0x20, frames[0] + 1)
            return result

        def pause():
            nonlocal sample
            sample += 1
            if sample < len(frames):
                struct.pack_into("<I", data, 0x20, frames[sample])

        return fields.sample_fields(pointer, read, 100, 300, 1072, pause), counts

    def test_bounded_progressing_samples_and_counter_wrap(self):
        for frames in [(5, 6, 7), (0xffffffff, 0, 1)]:
            report, counts = self.sample(frames)
            self.assertEqual(counts["bytes"], 1620)
            self.assertEqual(report["payload_bytes"], counts["bytes"])
            self.assertEqual(report["counter_advances"], 2)
            self.assertEqual(report["selected_state_changes"], 0)
            self.assertNotIn("position", report)
            self.assertNotIn("frame", report)

    def test_stalled_backwards_and_implausible_counter_changes_rejected(self):
        for frames in [(5, 5, 6), (5, 4, 3), (5, 20000, 20001)]:
            with self.assertRaisesRegex(ObservationError, "counter_not_advancing"):
                self.sample(frames)

    def test_torn_fields_and_replaced_source_stop_without_retry(self):
        for torn, source_change in [(True, False), (False, True)]:
            with self.assertRaisesRegex(fields.CoherenceError, "fields_changed_within_sample") as caught:
                self.sample((5, 6, 7), torn=torn, source_change=source_change)
            self.assertTrue(all(type(value) is bool for value in caught.exception.checks.values()))
            self.assertEqual(caught.exception.checks["source_pointer_stable"], not source_change)
            self.assertEqual(caught.exception.checks["selected_ranges_stable"], not torn)
            self.assertEqual(caught.exception.changed_ranges, ["buffer_counter_hypothesis"] if torn else [])


if __name__ == "__main__":
    unittest.main()
