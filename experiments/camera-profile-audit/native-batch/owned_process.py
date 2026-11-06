# SPDX-License-Identifier: MIT
"""Native batch API versus the existing reader, using an owned child only."""
import ctypes
import json
from pathlib import Path
import statistics
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from association import ReadOnlyProcess
from camera_fields import RANGES


class Result(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in
                ("version", "status", "checks", "changed_ranges", "read_calls", "payload_bytes")] + [
                (name, ctypes.c_uint64) for name in ("elapsed_microseconds", "candidate", "source")] + [
                ("first", ctypes.c_ubyte * 244), ("second", ctypes.c_ubyte * 244)]


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    check(ctypes.sizeof(Result) == 536, "local ABI size differs")
    library = ctypes.WinDLL(str(Path(sys.argv[1]).resolve()))
    batch = library.odv_read_selected_v1
    batch.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64,
                      ctypes.c_uint32, ctypes.POINTER(Result)]
    batch.restype = None
    producer = r'''
import ctypes, json, sys
source = (ctypes.c_ubyte * 0xb00)(*(i % 251 for i in range(0xb00)))
table = ctypes.c_uint64(0)
candidate = (ctypes.c_uint64 * 136)()
candidate[0] = ctypes.addressof(table)
candidate[1072 // 8] = ctypes.addressof(source)
root = ctypes.c_uint64(ctypes.addressof(candidate))
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]
kernel.VirtualAlloc.restype = ctypes.c_void_p
blocked = kernel.VirtualAlloc(None, 4096, 0x3000, 1)
executable = kernel.VirtualAlloc(None, 4096, 0x3000, 0x40)
if not blocked or not executable: raise RuntimeError('owned allocation failed')
print(json.dumps(dict(root=ctypes.addressof(root), table=ctypes.addressof(table),
                     candidate=ctypes.addressof(candidate), source=ctypes.addressof(source),
                     blocked=blocked, executable=executable)), flush=True)
sys.stdin.buffer.read(1)
'''
    child = subprocess.Popen([sys.executable, "-c", producer], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        context = json.loads(child.stdout.readline())
        with ReadOnlyProcess(child.pid, sys.executable) as reader:
            def acquire(root=context["root"], offset=1072):
                result = Result()
                batch(reader.handle, root, context["table"], offset, ctypes.byref(result))
                return result

            result = acquire()
            expected = b"".join(bytes((i % 251 for i in range(offset, offset + size)))
                                for offset, size in RANGES)
            check(result.version == 1 and result.status == 1 and result.checks == 31,
                  "native stable sample rejected")
            check(result.read_calls == 23 and result.payload_bytes == 540 and not result.changed_ranges,
                  "native sample bounds differ")
            check(bytes(result.first) == expected == bytes(result.second), "selected bytes differ")
            check(result.candidate == context["candidate"] and result.source == context["source"],
                  "local chain state differs")
            # Real Windows protection/range checks, not just injected failures.
            for root in (context["blocked"], context["executable"], 0x10001, 1, 0x7fffffffffff):
                rejected = acquire(root)
                check(rejected.status == 5 and rejected.read_calls == 1 and rejected.payload_bytes == 0,
                      "unreadable/executable/unaligned/noncanonical range accepted")
            check(acquire(offset=4104).status == 2, "unbounded offset accepted")
            limited = reader.api.OpenProcess(0x00100400, False, child.pid)
            check(bool(limited), "owned query-only process handle unavailable")
            try:
                rejected = Result()
                batch(limited, context["root"], context["table"], 1072, ctypes.byref(rejected))
                check(rejected.status == 5 and rejected.payload_bytes == 0,
                      "query-only handle unexpectedly read target bytes")
            finally:
                check(bool(reader.api.CloseHandle(limited)), "owned limited handle close failed")
            # No speed threshold: scheduling varies. Compare the same sequence
            # and report owned-fixture timing only, never game performance.
            native_times, python_times = [], []
            for _ in range(25):
                start = time.perf_counter_ns()
                candidate = reader.read_pointer(context["root"])
                check(reader.read_pointer(candidate) == context["table"], "owned table changed")
                source = reader.read_pointer(candidate + 1072)
                first = {offset: reader.read_bytes(source + offset, size) for offset, size in RANGES}
                second = {offset: reader.read_bytes(source + offset, size) for offset, size in RANGES}
                counter = reader.read_bytes(source + 0x20, 4)
                source_after = reader.read_pointer(candidate + 1072)
                table_after = reader.read_pointer(candidate)
                candidate_after = reader.read_pointer(context["root"])
                python_times.append((time.perf_counter_ns() - start) / 1000)
                check(first == second and first[0x20] == counter and source == source_after and
                      table_after == context["table"] and candidate == candidate_after, "owned Python mismatch")
                start = time.perf_counter_ns()
                result = acquire()
                native_times.append((time.perf_counter_ns() - start) / 1000)
                check(result.status == 1 and bytes(result.first) == expected and result.payload_bytes == 540,
                      "native comparison changed")
            reader.active()  # The batch API must leave its borrowed handle open.
            child.stdin.close()
            child.wait(timeout=10)
            rejected = acquire()
            check(rejected.status == 5 and rejected.read_calls == 1 and rejected.payload_bytes == 0,
                  "exited owned process was accepted")
        check(reader.handle is None, "borrowed process handle not closed by owner")
    finally:
        child.stdin.close()
        child.wait(timeout=10)
        child.stdout.close()
        child.stderr.close()
    check(child.returncode == 0, "owned child failed")
    # No identity, addresses or byte payload in stdout.
    print(json.dumps({"evidence_kind": "owned_child_selected_read_comparison", "status": "PASS",
                      "samples_per_method": 25, "payload_bytes_per_sample": 540,
                      "native_median_microseconds": statistics.median(native_times),
                      "python_median_microseconds": statistics.median(python_times),
                      "scope": "Owned stable producer only; no game access, camera validity or atomicity proof."}))


if __name__ == "__main__":
    main()
