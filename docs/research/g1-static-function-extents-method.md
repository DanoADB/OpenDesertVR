# Static function extents for existing camera-source seeds

The current executable already has one unique reference-pattern match and two
unique ordered pointer-table function matches from the pinned public profile.
That establishes static leads, not a camera writer or callable integration API.
This optional experiment asks whether PE exception metadata bounds those three
seeds and permits a small, reproducible inspection of their instruction mix.
It does not sweep cross-references or traverse a call graph.

## Range and input contract

Use the existing bounded x64 PE reader and Microsoft's
[PE exception-directory format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-pdata-section).
Require 1..500,000 complete twelve-byte runtime-function records in read-only
file-backed metadata. Records must be ordered, non-overlapping, entirely within
file-backed executable sections and reference aligned file-backed unwind data.
Require a unique half-open containing range for each seed, at most 64 KiB per
selected function, and a supported version in its unwind header. This is not a
full unwind-program validator; missing metadata does not prove code is absent.

Retain the exact current executable/build/catalog guards and the pinned profile
SHA-256. Re-derive unique seeds and their read-only table relationship; do not
copy old locations to the new build or invent a fallback. Check input hashes,
file identity and metadata before and after observation. Reserve output
exclusively, recording partial stage and sanitized failure without retry.

The explicit installed MSVC dumpbin.exe must match its supplied SHA-256 before
and after use. The reviewed local copy has a valid Microsoft signature and
SHA-256 cfee7ed764b7b0866aae0be12f9c0ad2a7f5aecc4046ba2b7de26335827ca815.
Use its documented [NOBYTES disassembly](https://learn.microsoft.com/en-us/cpp/build/reference/disasm)
with a /RANGE containing explicit hexadecimal virtual addresses. Each call has
a ten-second timeout and accepts at most 1 MiB of output. This is not a memory
sandbox or an attestation of every tool dependency. The tool may read other
file metadata internally; only the selected ranges may appear as instructions.

Only ordered instruction lines starting at the selected function entry and
remaining within its range are accepted. Export sizes, function fingerprints,
seed-entry booleans and mnemonic counts. Do not retain instruction bytes,
locations, operands, assembly listings or raw tool output. No game program,
upstream mod or captured instruction is executed; no process access, game-file
write, loading change or new package dependency is involved.

## Owned qualification

Four standard-library tests use original synthetic PE bytes to cover directory
bounds/permissions, missing directories, record ordering/overlap, invalid code
and unwind ranges, half-open boundaries, unsupported unwind versions and
ordered/in-range instruction parsing with operand omission.

An original native fixture exports a calling function that writes an owned
state. Its executable is inspected, not run by the test. Both local Debug and
Release qualification pass: export entry matches the metadata range, bounded
installed-tool decoding sees the call, and changed tool fingerprints reject.
Initial owned qualification exposed a missing hexadecimal prefix in /RANGE and
Debug export alias formatting; both were fixed before any game-file inspection.
CI builds and inspects only that owned fixture and runs the synthetic tests.
The helper has no install target or linkage to M0.

## Interpretation

Function extents and linear decoding do not identify every reachable instruction,
writer, caller, object lifetime or publication protocol. Calls and indirect
control flow remain untraversed. A unique table and valid instruction mix do not
establish a C++ type, camera semantics or safe callback. The next design needs
an actual producer/consumer relationship rather than treating this metadata as
permission for a live probe. Camera control, extra world rendering and G2 stay
unverified. Headset tests remain deferred.
