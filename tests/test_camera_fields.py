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
            diagnostic = caught.exception.copy_diagnostics
            self.assertEqual(diagnostic["status"], "DIAGNOSTIC_ONLY_UNCOHERENT" if torn else
                             "NOT_EVALUATED_POINTER_CHAIN_CHANGED")
            if torn:
                self.assertEqual(diagnostic["copies"], [{"status": "NUMERIC_CHECKS_PASSED"}] * 2)

    def test_rejected_copy_diagnostics_keep_independent_numeric_failures(self):
        first, second = selected(buffer()), selected(buffer(6))
        second[0xac0] = struct.pack("<f", 1)
        checks = {"global_stable": True, "source_pointer_stable": True, "table_pointer_stable": True}
        report = fields.diagnose_copies(first, second, checks)
        self.assertEqual(report["status"], "DIAGNOSTIC_ONLY_UNCOHERENT")
        self.assertEqual(report["copies"], [{"status": "NUMERIC_CHECKS_PASSED"},
                         {"status": "NUMERIC_CHECKS_REJECTED", "reason": "candidate_layout_sentinel_mismatch"}])
        self.assertEqual(first, selected(buffer()))
        for key in checks:
            changed = dict(checks, **{key: False})
            self.assertEqual(fields.diagnose_copies(first, second, changed),
                             {"status": "NOT_EVALUATED_POINTER_CHAIN_CHANGED"})

    def test_equal_numeric_copies_can_mix_two_valid_producer_states(self):
        # Counterexample, not a game protocol: this producer's token does not
        # cover geometry updates. Both complete states are numerically valid.
        frame = 5

        def states():
            first, second = buffer(frame), buffer(frame)
            struct.pack_into("<f", second, 0x80, 11)
            struct.pack_into("<f", second, 0x410, -11)
            struct.pack_into("<f", second, 0x4e0, 1.6875)
            struct.pack_into("<f", second, 0x4e0 + 5 * 4, 3)
            return first, second

        original, moved = states()
        first_state, second_state = fields.decode(selected(original))[1], fields.decode(selected(moved))[1]
        observed = []

        def read(address, size):
            offset = address - 4000
            a, b = states()
            # Each read sees one complete producer state. Only projection is
            # sampled in state B; all other ranges are sampled in state A.
            data = b if offset == 0x4e0 else a
            value = bytes(data[offset:offset + size])
            observed.append((offset, value))
            return value

        def advance():
            nonlocal frame
            frame += 1

        report = fields.sample_fields(lambda address: {100: 200, 200: 300, 1272: 4000}[address],
                                      read, 100, 300, 1072, advance)
        mixture = dict(observed[:len(fields.RANGES)])
        mixed_state = fields.decode(mixture)[1]
        self.assertNotIn(mixed_state, (first_state, second_state))
        self.assertEqual(report["status"], "OBSERVED_NUMERIC_CONSISTENCY")
        self.assertIn("do not prove", report["scope"])


if __name__ == "__main__":
    unittest.main()
