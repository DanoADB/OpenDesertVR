# SPDX-License-Identifier: MIT
# Selected layout/validation rules adapted from CrimsonDesertTelemetry at
# 434b3b96646b410ab8396483e31c538e051f3a4e. Copyright (c) 2026
# CrimsonDesertTelemetry contributors. See docs/licenses/CrimsonDesertTelemetry-MIT.txt.
"""Numeric consistency checks for an explicitly unverified historical layout."""
import math
import struct
import time

from association import ObservationError

# Facts from the reviewed direct-layout decoder. No whole-buffer copy, fields
# outside these ranges, raw far value, lighting arrays or game assets are read.
RANGES = ((0x20, 4), (0x30, 16), (0x80, 12), (0x90, 12),
          (0x3e0, 128), (0x4e0, 64), (0x860, 4), (0xac0, 4))
BYTES_PER_ATTEMPT = 6 * 8 + 2 * sum(size for _, size in RANGES) + 4
RANGE_ROLES = ("buffer_counter_hypothesis", "resolution_hypothesis", "position_hypothesis",
               "forward_hypothesis", "view_matrices_hypothesis", "projection_hypothesis",
               "near_hypothesis", "layout_sentinel_hypothesis")


class CoherenceError(ObservationError):
    def __init__(self, checks, changed_ranges, copy_diagnostics):
        super().__init__("candidate_fields_changed_within_sample")
        self.checks = checks
        self.changed_ranges = changed_ranges
        self.copy_diagnostics = copy_diagnostics


def decode(fields):
    if set(fields) != {offset for offset, _ in RANGES} or any(
            len(fields[offset]) != size for offset, size in RANGES):
        raise ObservationError("selected_field_ranges_incomplete")

    def floats(offset):
        return struct.unpack("<" + "f" * (len(fields[offset]) // 4), fields[offset])

    values = {offset: floats(offset) for offset, _ in RANGES if offset != 0x20}
    if not all(math.isfinite(value) for vector in values.values() for value in vector):
        raise ObservationError("candidate_fields_nonfinite")
    width, height, inverse_width, inverse_height = values[0x30]
    if any(not 1 <= size <= 32768 or size != int(size) or abs(size * reciprocal - 1) > 1e-5
           for size, reciprocal in [(width, inverse_width), (height, inverse_height)]):
        raise ObservationError("candidate_resolution_inconsistent")
    if values[0xac0][0] != 6360000.0:
        raise ObservationError("candidate_layout_sentinel_mismatch")
    position, forward = values[0x80], values[0x90]
    view, relative = values[0x3e0][:16], values[0x3e0][16:]
    right, up = tuple(view[i] for i in (0, 4, 8)), tuple(view[i] for i in (1, 5, 9))

    def dot(a, b):
        return sum(x * y for x, y in zip(a, b))

    def distance(a, b):
        return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))

    cross = (right[1] * up[2] - right[2] * up[1], right[2] * up[0] - right[0] * up[2],
             right[0] * up[1] - right[1] * up[0])
    if (any(abs(dot(v, v) - 1) >= .005 for v in (right, up, forward)) or
            any(abs(dot(a, b)) > .01 for a, b in ((right, up), (right, forward), (up, forward))) or
            abs(dot(cross, forward) - 1) > .01):
        raise ObservationError("candidate_basis_inconsistent")
    translation = tuple(-dot(position, axis) for axis in (right, up, forward))
    if (distance(view[12:15], translation) > .05 or distance(tuple(view[i] for i in (2, 6, 10)), forward) > .001 or
            any(abs(matrix[i]) > .0001 for matrix in (view, relative) for i in (3, 7, 11)) or
            any(abs(matrix[15] - 1) > .0001 for matrix in (view, relative)) or
            distance(relative[12:15], (0, 0, 0)) > .001 or
            any(abs(view[i] - relative[i]) > .0001 for i in range(12))):
        raise ObservationError("candidate_view_matrices_inconsistent")
    projection = values[0x4e0]
    if (any(abs(projection[i]) > .0001 for i in (1, 2, 3, 4, 6, 7, 8, 9, 12, 13, 15)) or
            abs(projection[11] - 1) > .0001 or projection[0] <= 0 or projection[5] <= 0):
        raise ObservationError("candidate_projection_layout_unsupported")
    fov, aspect = 2 * math.atan(1 / projection[5]), projection[5] / projection[0]
    if not .2 <= fov <= 3 or not .5 <= aspect <= 5 or not .001 <= values[0x860][0] <= 10:
        raise ObservationError("candidate_projection_values_implausible")
    # Internal comparison state only. Never serialize raw vectors/matrices/counter.
    state = position + forward + view + relative + projection
    return struct.unpack("<I", fields[0x20])[0], state


def diagnose_copies(first, second, checks):
    """Classify existing bytes only; never award coherent-sample acceptance."""
    if not all(checks[key] for key in ("global_stable", "source_pointer_stable", "table_pointer_stable")):
        return {"status": "NOT_EVALUATED_POINTER_CHAIN_CHANGED"}
    copies = []
    for fields in (first, second):
        try:
            decode(fields)
        except ObservationError as error:
            copies.append({"status": "NUMERIC_CHECKS_REJECTED", "reason": str(error)})
        else:
            copies.append({"status": "NUMERIC_CHECKS_PASSED"})
    return {"status": "DIAGNOSTIC_ONLY_UNCOHERENT", "copies": copies,
            "scope": "Checks on each existing byte copy, not an atomic or coherent snapshot. No counter progression or camera semantics established."}


def sample_fields(read_pointer, read_bytes, global_address, expected_table, source_offset, pause=lambda: None):
    if type(source_offset) is not int or not 8 <= source_offset <= 4096 or source_offset % 8:
        raise ObservationError("unbounded_source_offset_hypothesis")
    previous_chain = previous_frame = previous_state = None
    changes = advances = 0
    for _ in range(3):
        candidate = read_pointer(global_address)
        if not candidate or read_pointer(candidate) != expected_table:
            raise ObservationError("candidate_association_unavailable")
        source = read_pointer(candidate + source_offset)
        if not source:
            raise ObservationError("candidate_source_unavailable")
        first = {offset: read_bytes(source + offset, size) for offset, size in RANGES}
        second = {offset: read_bytes(source + offset, size) for offset, size in RANGES}
        frame_after = read_bytes(source + 0x20, 4)
        source_after = read_pointer(candidate + source_offset)
        table_after = read_pointer(candidate)
        candidate_after = read_pointer(global_address)
        checks = {"global_stable": candidate == candidate_after, "source_pointer_stable": source == source_after,
                  "table_pointer_stable": table_after == expected_table, "selected_ranges_stable": first == second,
                  "counter_bracket_stable": frame_after == first[0x20]}
        if not all(checks.values()):
            changed = [role for (offset, _), role in zip(RANGES, RANGE_ROLES) if first[offset] != second[offset]]
            raise CoherenceError(checks, changed, diagnose_copies(first, second, checks))
        frame, state = decode(first)
        chain = (candidate, source)
        if previous_chain is not None:
            if chain != previous_chain:
                raise ObservationError("candidate_source_replaced")
            delta = (frame - previous_frame) & 0xffffffff
            if not 1 <= delta <= 10000:
                raise ObservationError("candidate_buffer_counter_not_advancing_plausibly")
            advances += 1
            changes += state != previous_state
        previous_chain, previous_frame, previous_state = chain, frame, state
        pause()
    return {"status": "OBSERVED_NUMERIC_CONSISTENCY", "stable_bracketed_samples": 3,
            "counter_advances": advances, "selected_state_changes": changes,
            "selected_ranges": len(RANGES), "payload_bytes": BYTES_PER_ATTEMPT * 3,
            "layout_hypothesis": "pinned_upstream_direct_scene_constants",
            "scope": "Published offsets tested as hypotheses. Numeric/view/projection consistency and counter progression do not prove active-camera semantics, units, atomicity, write control or stereo."}


def observe_fields(reader, global_rva, table_rva, image_size, source_offset):
    if reader.image_size != image_size:
        raise ObservationError("loaded_image_size_mismatch")
    global_address, table = reader.base + global_rva, reader.base + table_rva
    reader.region(global_address, image_base=reader.base, writable=True)
    reader.region(table, image_base=reader.base, writable=False)
    deadline = time.monotonic() + 1.0

    def pointer(address):
        if time.monotonic() > deadline:
            raise ObservationError("field_sampling_deadline_expired")
        return reader.read_pointer(address)

    def bytes_at(address, size):
        if time.monotonic() > deadline:
            raise ObservationError("field_sampling_deadline_expired")
        return reader.read_bytes(address, size)

    return sample_fields(pointer, bytes_at, global_address, table, source_offset, pause=lambda: time.sleep(.05))
