# Owned draw ground truth

## GPU constant snapshots

`constant_snapshot.hpp` is an original experimental component for the next
camera-value acquisition step. It records a GPU copy of up to 4 KiB from a
valid UPLOAD buffer, retaining its resource reference. Other heap types, bundle
command lists, oversized sources/ranges and more than six pending captures are
rejected. No source resource is written or transitioned.

The integration must supply a fence causally ordered **after actual execution
submission of that copy**. `submitted_after_execute` records this caller
assertion; it cannot detect an unrelated fence. A before-execute observer
callback or a listed draw is insufficient. `read_completed` refuses data until
that fence completes. `discard_unsubmitted` is valid only when the caller has
proved the recording will never execute. These obligations still need a
separately reviewed game integration; this component is not loaded in the game.

Unknown completion quarantines resource references and retains the capacity
slot instead of freeing GPU-referenced memory. Capture stops once six such
slots are occupied. Sources are limited to 64 MiB each; retaining a reference
can prolong an existing allocation's lifetime. This behavior is an experimental
failure containment measure, not production resource recovery or a watchdog.

The native `owned_gpu_constant_snapshot` check runs Direct, Compute and Copy
queues on WARP. It records a copy, then changes the upload data before
submission: GPU readback must contain the late values rather than the earlier
CPU sample. A queue gate verifies pending-fence reads stay unavailable. An
unsubmitted second list stays unread even after the first list's fence
completes. Default heaps, invalid ranges and capacity overflow are rejected.
This validates owned GPU acquisition semantics, not game camera values.

## Draw controls

This optional original SDK-only project establishes software-GPU pixel ground
truth before Devkit draw metadata is compared. It has no install target, M0
runtime linkage, game access, OpenXR, capture callbacks or external source.

Build with `cmake -S experiments/draw-binding-owned -B out/build/draw-binding-owned -A x64`,
then `cmake --build out/build/draw-binding-owned --config Release` and
`ctest --test-dir out/build/draw-binding-owned -C Release --output-on-failure`.
Debug is also supported. Requires Windows x64, the Windows SDK, Python and WARP.
No automatic hardware fallback is allowed. CI runs both configurations.

The authored SM 5.1 shaders use vertex b2/space3 and pixel b5/space7. Eight
distinct upload buffers remain alive and persistently mapped, with no writes
after initialization. Sizes are 256 through 2048 bytes. Root CBVs produce the
red draw. Single-entry descriptor tables, with vertex-only/pixel-only visibility,
produce green then blue using distinct heap slots. Descriptors are never
overwritten while referenced. All vertex offsets are zero in this baseline;
it does not independently validate changing vertex constants or camera values.

Four procedural triangles cover four 16-pixel columns of a 64 by 32 target.
List A contains red, green and blue. List B contains yellow. List C copies the
target to a readback buffer. Baseline submits A,C, leaving B recorded but
unsubmitted. Its expected columns are RGB/black. A paired positive execution
control submits A,B,C and expects RGB/yellow. The same B recording path is used
in both. Every RGBA byte in all 2048 pixels is compared after a queue fence,
for two frames per case; each frame resets the same list/allocator objects
only after completion. Fence wait is bounded to five seconds.

The harness gives each run fresh retained directories, imposes a 20-second
child timeout per case, checks exact typed results, fingerprints the executable
before/after, and verifies identical owned shader SHA256/CRC32 identities in
both controls. Capture DLLs beside the executable are rejected. This is not an
attestation of every module Windows may load. Native resource pointers are
retained only in `ground-truth.private.json`; shader blobs are original owned
assets. None are installed into the M0 package. Do not publish private handles.

An explicit `--loop` mode runs the same offscreen pixel check while presenting
a separate black 64 by 32 swapchain through a hidden window. It stops on a
`stop.flag` in the output directory's parent or after 60 seconds, writes
`ready.flag` after the first verified/presented frame, and keeps all resource
identities alive until exit. Unlike baseline mode, it permits a local graphics
proxy so a separately qualified controller can stage its pinned observer.
Its result labels capture-tool execution as EXTERNAL_OBSERVER_POSSIBLE, never
as verified by the fixture itself. No loop mode is installed into M0.

`--loop-dxil` uses the same control geometry with fixed local files
`owned.dxil.vs.cso` and `owned.dxil.ps.cso` beside the executable instead of
the built-in FXC compilation. They must be generated from this fixture's
original HLSL by a separately fingerprinted compiler and staged/hash-checked
by the qualification controller. Reads are bounded to 64 KiB per shader.
This isolates SM6.6/DXIL binding behavior from the older SM5.1 parser; it is
not an arbitrary shader or game-asset test. Default CI controls remain SM5.1.

`--loop-dxil-budget` records 150 submitted geometry draws per frame by repeating
the same red/green/blue controls 50 times, plus the unsubmitted yellow draw.
The expected pixels remain RGB/black. This exercises a metadata observer's
retained-record cap without increasing image dimensions or changing resources.

`--loop-dxil-capture` preserves the same RGB/black pixels but binds the
unsubmitted yellow control through finite descriptor-table CBVs too. This
gives the GPU-copy observer a finite, active 256-byte PS range on green, blue
and unsubmitted yellow. Existing baseline/root-CBV controls are unchanged.

The pixel baseline does **not** execute Devkit, qualify its CBV resource identities,
verify its submission metadata, or observe per-draw GPU constant bytes. No
headset/game test is run. Compare the exact authored shader pair and known bindings through the
already qualified owned Devkit transport. Known pixel buffers must validate
the native/ReShade handle convention before interpreting other missing/stale
bindings. A listed draw alone cannot establish GPU submission or completion.

`constant_snapshot.hpp` and `constant_capture_driver.hpp` provide the bounded
UPLOAD-only GPU-copy path being prepared for camera research. Each copy is at
most 4096 bytes; at most six unresolved copies retain resources. The driver
tracks every execution, including resubmission outside an active snapshot.
Only a later AFTER-present callback on the same native queue and CPU thread
can establish its post-execution fence. Other queues/threads cannot grant
readiness. The first completed CPU sample is immutable; it belongs only to
the draw where that copy was inserted, not subsequent uses of the buffer.

GPU completion alone cannot free the allocation: a recorded command list may
execute again. Explicit retirement and completion of the latest execution are
both required. A BEFORE-reset callback alone is insufficient; the driver waits
for subsequent recording (assuming conforming D3D12 use), or destruction.
Unsubmitted retired lists discard their copies. Unknown completion/retirement
retains allocations and consumes the fixed capacity rather than freeing memory
still referenced by commands. This can stop capture; it is not production
fault recovery or a hard watchdog.

`owned_gpu_constant_snapshot` exercises actual WARP Direct, Compute and Copy
queues: late upload writes, blocked fences, repeated execution, retirement,
wrong queue/thread rejection and an unsubmitted control. These tests do not
execute ReShade callbacks or establish actual game-camera contents. Addon
wiring still requires its own owned runtime qualification before game use.

`owned_constant_copy.hpp` is the next upload-input observation primitive. It
submits a single exclusively owned copy list on the application's actual
queue, then signals its own fence after that native submission. It can operate
on Direct, Compute or Copy queues without waiting for presentation. Queue waits
already issued by the app remain before the helper copy. Only UPLOAD sources
qualify. No application recording is modified or reused by this primitive.

The next integration must match a known DEFAULT candidate's destination range
to an actual application upload copy, retain the valid UPLOAD source, and
sample it on that same queue before the upcoming application submission.
This captures upload input, not an arbitrary later DEFAULT-buffer state or
per-draw camera contents. Partial writes, GPU-written sources and missing
provenance must stay unavailable. Native tests cover blocked queues and a late
CPU upload write on all three queue types. The separate pinned observer has
since returned one 768-byte subsequent upload input in an actual world scene;
camera semantics remain unverified. See
[world evidence](../../docs/evidence/g1-game-world-input-2026-11-06.json).

`upload_input_observer.hpp` matches covering uploads to at most six registered
DEFAULT candidate ranges. It keeps valid strong resource references, rejects
partial/overflowing ranges and non-UPLOAD inputs, and submits the owned copy
on the actual application's queue before that list executes. Registration
permits matching for five seconds; it does not impose a hard driver timeout.
Only the originating candidate draw owns the ticket. Reset followed by new
recording or Close retires an unsubmitted match; an empty replacement list
cannot grant stale data. Completed samples are upload inputs, not proof of
the destination's later contents or camera semantics.

`--loop-dxil-upload` gives green/blue and unsubmitted yellow real DEFAULT PS
buffers, populated from original UPLOAD controls via explicit application
copies and barriers. RGB/black pixels are checked independently. Native
Direct/Compute/Copy tests also compare a matched subrange against the actual
DEFAULT destination readback and reject an unsubmitted/empty-reset control.
Separate addon callback qualification is still required before game use.

`KnownBufferStates` and `ConstantSnapshot::record_default` add an experimental
owned-only route to read a DEFAULT constant range directly. Exact native legacy
barrier records establish a same-recording read state. The copy temporarily
adds COPY_SOURCE when needed and restores the original read state. Unknown,
COMMON, write, incomplete split and invalidated states are rejected. Aliasing,
destruction, unsupported barrier APIs, recording replacement and overflow must
invalidate the appropriate state proof. This does not track global queue state.

The actual WARP control verifies late upload data reaching DEFAULT, split BEGIN
rejection and matched END acceptance, blocked-fence rejection, exact readback
and an independent application copy after state restoration. A second pass
uses an already copy-readable state. Debug-layer errors are rejected when the
layer is available; availability is reported explicitly. Separate metadata
controls reject missing/mismatched split endings and invalidated/write states.

This route is NOT integrated into the game observer. The pinned ReShade generic
barrier callback drops native split flags and enhanced buffer ranges, so it
cannot supply this contract. A native metadata integration must first preserve
these details, invalidate unsupported cases and pass owned runtime controls.
Do not pass descriptor usage or a guessed before-state as a substitute.
