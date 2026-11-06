# G1 bounded read-only pointer association

Method reviewed 2026-11-06 after the
[offline relationship check](../evidence/g1-camera-relationships-2026-11-06.json).
This is a distinct observation method, not another full-frame capture attempt.
The production game adapter remains inactive. No headset is required.

## Question and scope

Does the current executable's derived global point to an object whose first
pointer equals the independently derived current-build function-pointer table?
A positive result establishes that pointer association during sampling only.
It does not establish camera semantics, active rendering, field layout, pose,
projection control, frame ownership, world-render scheduling or stereo.

The original optional probe uses normal Windows read access to an explicitly
selected PID. It has no game launcher, memory scanner, debugger, hook, remote
thread, write API or privilege adjustment. Normal denial or unexpected memory
properties stop the experiment. No game/protection check is patched or forced.

## Preconditions and observations

1. Require the exact reviewed executable/Steam-build pair, ready installation
   and matching installation path through the existing independent build gate.
   Require the pinned external profile hash; never execute its source.
2. Derive the candidate global and pointer table afresh from the fingerprinted
   file using unique validated instruction/function relationships. Use none of
   the historical object's field offsets, old global/table locations, cached
   heap pointers or automatic upstream recovery.
3. Open the selected process with VM_READ, QUERY_INFORMATION and SYNCHRONIZE.
   Match its full image path, unique main module and image size to the selected
   file. Keep the same process handle; do not search for or fall back to another
   PID. File hashing/path/size checks do not cryptographically hash live memory.
4. Require the global's live range to be committed writable non-executable
   image data and the table's range to be committed read-only image data, both
   in the expected module allocation. Query only the selected ranges.
5. Take three bracketed samples: global, object first pointer twice, global
   again. Require the global and object pointer values to agree within and
   between samples and equal the expected table. Only twelve eight-byte values
   are read, at most 96 bytes. No pose, source-pointer or counter fields are read.
6. Reject null/invalid pointers, unreadable/guarded/executable memory, short
   reads, mismatches, replacement, target exit or deadline expiration. Do not
   retry failure or alter permissions. Sampling has a one-second cooperative
   deadline; OS calls are not a separately enforced process watchdog.
7. Revalidate disk inputs and target liveness before recording success; close
   the process handle. Reserve the output exclusively before process access.
   Export only hashes, status, counts and limits; no PID, address, path or raw
   game data. Keep camera/control/stereo capabilities unverified.

## API basis and qualification

Microsoft documents [ReadProcessMemory](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-readprocessmemory)
as requiring VM_READ and failing for inaccessible ranges.
[VirtualQueryEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex)
requires QUERY_INFORMATION; it is used for the selected range properties.
The probe requests the [minimum rights](https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights)
needed for those calls and liveness checks. It does not request ALL_ACCESS.
[Module enumeration](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-enumprocessmodulesex)
is bounded to 512 modules; inconsistent or unavailable metadata is rejected.

An actual owned Python child-process fixture exercises the native API wrapper:
module identity, stable pointer association, no-access memory rejection, path
mismatch rejection, handle closure and cooperative child exit 0. Synthetic
tests reject changing/null/mismatched pointers and read errors without retry.
The CLI preflight test verifies no process access for an invalid executable and
preservation of existing reports. These checks qualify the observer, not a game.

The separate full ASI/lighting package is not needed, installed or executed.
The method observes normal process memory and leaves the game unmodified.
Any later camera-field observation or intervention needs its own scope review
and actual evidence; this result cannot authorize guessed layout assumptions.
