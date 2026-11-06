# Observer-side selected-read batch prototype

The sequential field observer rejected coherence on each actual game attempt.
Its [latest diagnostic](g1-camera-field-hypothesis-method.md) found two
numerically plausible individual copies but no coherent sample. Counter meaning
and the exact cause of disagreement remain unknown. A faster acquisition path
is a hypothesis to test, not a diagnosed fix.

## Scope and unchanged checks

The optional C++20 experiment at experiments/camera-profile-audit/native-batch
executes one existing read sequence in one observer-side native call. Its DLL
loads into the owned observer only. There is no game loading, injection, hook,
writer, privilege adjustment, process opening, permission change or network API.
It has no install target and is not linked into M0 or the production adapter.

The caller lends an already opened read-only process handle. The prototype does
not close it. A future game caller must retain all existing reviewed
build/profile/file/path/module-size and current-derived image-region gates,
including global writable-image/table read-only-image properties; this low-level
routine alone does not establish target identity. The current tests target an
owned child and do not constitute approval or evidence of game compatibility.

Each call performs at most 23 explicit reads, 540 payload bytes, using exactly
the existing eight selected ranges twice, a final counter bracket and six
pointer reads. Each read checks process liveness, canonical range/alignment,
committed readable non-executable memory, protection/region bounds, exact byte
count and subsequent liveness. No protection is changed. Fail on any error,
deadline or null/mismatched initial chain; no automatic retry or stale fallback.
The one-second cooperative deadline does not bound a blocking Windows call.

Global/source/table equality, selected-copy equality and the final counter
bracket retain their separate boolean checks. The fixed result also includes
changed-range bits. It does not perform numeric validation or between-sample
counter progression; those existing checks must still be applied by any caller.
OBSERVED_EQUAL means the bracket comparisons matched, never an atomic snapshot,
camera-field validity, active camera semantics, writes or stereo. Sequential
external reads cannot prove producer synchronization without a known protocol.

The versioned local ABI holds raw selected copies and local chain identities in
observer memory only. Tests print sanitized counts/timing, not raw values or
locations. Do not serialize the ABI structure. A future game-facing wrapper
also needs explicit source/library provenance, layout/version validation,
bounded series/deadline and the existing numeric/progression rejection rules.
That wrapper is not implemented by this prototype.

## Owned qualification

Deterministic C++ producer fixtures mutate each selected range, the global,
source and table separately, and the counter after both copies. Every conflict
rejects. Failure injected at every one of 23 reads stops at that call, preserving
the exact completed byte count. Null roots/sources, initial table mismatch,
offset bounds, overflow and cooperative deadline also reject.

An actual Windows child holds known original bytes. Both readers return the
same selected bytes. The native API rejects no-access, executable, misaligned,
noncanonical and low ranges, a handle lacking VM_READ, and an exited child.
The owning reader verifies its borrowed handle remains live and closes it;
the child exits on cooperative stdin EOF. No game or headset is used.

Commands from the repository root, with CMake/MSVC and x64 Python available:

```powershell
cmake -S experiments/camera-profile-audit/native-batch -B out/build/selected-read -A x64
cmake --build out/build/selected-read --config Debug
ctest --test-dir out/build/selected-read -C Debug --output-on-failure -V
cmake --build out/build/selected-read --config Release
ctest --test-dir out/build/selected-read -C Release --output-on-failure -V
```

Both suites passed locally in both configurations. An initial 25-pair Release
comparison measured native median 45.6 microseconds versus Python 157.0, including
observer-call overhead. The fixed order and stable fixture are not a controlled
game benchmark; scheduling, load and target behavior differ. There is no speed
threshold in CI, and faster reads do not establish atomicity or explain the
earlier game failures. No native-batch game observation has been performed.
