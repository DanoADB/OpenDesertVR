# Reading a known DEFAULT constant range

The live world experiment returned upload input for a 768-byte binding, while
the shared 2816-byte DEFAULT ranges remained unread. The next acquisition route
copies a bounded range from the actual GPU resource at a known command-recording
point. This method is implemented and tested on owned resources only. It is
not enabled in the game observer and does not establish camera semantics.

`KnownBufferStates` consumes exact native legacy transition records after they
are recorded. It is local to one command recording. A full transition or matched
split END establishes the after-state; a pending/missing/mismatched split does
not. Aliasing, resource destruction/recreation and unsupported barrier APIs
invalidate affected entries. Unknown coverage invalidates a range. Recording
replacement clears all entries. Overflow stops tracking until recording reset.
The caller must serialize recording and report every relevant invalidation.
This is not a global queue-state tracker or an execution-order proof.

`ConstantSnapshot::record_default` requires an observed read-only constant-buffer
state on a Direct command list and an actual DEFAULT buffer with a valid range.
The existing six-copy, 4 KiB range and 64 MiB source-allocation bounds apply.
If COPY_SOURCE is absent, add it with a native transition, record the readback
copy and restore the exact prior state before the application continues. All
allocation precedes the transition; the intervening native commands do not
throw C++ exceptions. No source bytes are written. Unknown/COMMON/write states,
other queues and unsupported heap types remain unavailable.

The original submission, fence, repeated-execution, recording-retirement and
deferred-release requirements still apply. Recording alone grants no bytes.
The owned WARP test rejects reads behind a blocked fence, then compares the
completed capture with authored data copied through DEFAULT and with an
independent application readback after restoration. It exercises both a
temporary COPY_SOURCE addition and an already copy-readable state. Actual split
BEGIN/END barriers are included. Separate metadata tests reject missing and
mismatched split endings, alias invalidation, write states and forgotten resources.
Local Debug and Release each pass all three optional native tests. The local
D3D12 debug layer is unavailable; no debug-layer acceptance is claimed locally.
The test enables and checks the layer when available on the host.

Hosted CI for d3a3f42 passed Debug and Release. The downloaded logs explicitly
report debug=ENABLED for the DEFAULT test in both configurations. This is
owned-resource validation, not game validation.

## Native integration still required

The pinned ReShade wrapper calls its generic barrier event after the native
call, but its legacy conversion omits split flags; its enhanced conversion
omits sync fields and buffer offsets/sizes. These callbacks cannot supply this
method's contract. Do not infer a completed transition from a converted usage
mask or copy a game buffer using descriptor usage as an actual state.

A subsequent integration must preserve native metadata, handle reset,
destruction, aliasing and unsupported enhanced/bundle cases, and qualify that
actual callback route on owned resources before any game use. Preserve the
live observer9 case until normal game exit; do not replace loaded files or
interrupt unsaved gameplay. Empty snapshots are invalid scene observations,
even when a legacy collector labels its metadata transaction successful.

## Qualified native bridge

The separately built, hash-pinned ReShade 6.7.3.9004 UNOFFICIAL wrapper now
forwards native barrier arrays and an event after native ExecuteCommandLists
returns. A versioned private-data sink is registered on each command-list API
object. Arrays are borrowed during the synchronous callback only. Reset,
ClearState, bundle execution and protected-session changes invalidate metadata.
The addon forgets resource identities on destruction before device-data locks.
Sink pointers are cleared before command-list state replacement or destruction.
Registry entries are weak, bounded and contain no COM resource references.

Observer10 enables DEFAULT reads only with explicit ODVDefaultConstants=1.
Native state is serialized per command recording. Enhanced barriers currently
invalidate state; their usage masks are never converted into legacy proof.
Exact after-execute callbacks signal covering fences on the native application
queue. The present-based fallback is disabled in this mode. A capture can span
at most four empty presentation intervals before completing with no records;
the collector must reject an empty scene observation.

Four actual [owned runtime controls](../evidence/g1-owned-native-default-2026-11-06.json)
passed: ordinary legacy barriers, split legacy transitions, two deterministically
gated empty Presents before real draws, and rejection with the stock wrapper
that lacks this native contract. The positive cases returned authored green/blue
DEFAULT bytes after submission, while recorded unsubmitted yellow returned no
bytes. The missing-contract case recorded no copies. Every case independently
verified unchanged RGB/black pixels, normal exit, zero native device-reference
warnings, source/binary postflight and empty asset directories. Native runtime
qualification is separate from the component's debug-layer CI result.

Use the pinned native source/patch/dependency/generated-input lock and the
separate devkit-native-default input lock. These are research builds, not an
official ReShade binary or an M0 runtime dependency. The live observer9 game
session remains unchanged until normal exit. Next game observation should use
graphics-only exact 2816-byte mode, upload-input matching disabled and at most
six details from the 128-record capture. First establish actual native state
coverage and byte availability; do not infer camera identity from byte size.

Primary references: [Microsoft resource-state and split-barrier rules](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12),
[pinned ReShade native wrapper](https://github.com/crosire/reshade/blob/4a50d1eddace85734871d91792ff214f13f66c01/source/d3d12/d3d12_command_list.cpp).

## Enhanced-buffer path (2026-11-09)

The [actual still-world capture](../evidence/g1-game-native-default-2026-11-09.json)
confirmed native enhanced callbacks and after-execute delivery, but returned no
DEFAULT bytes. Observer10 invalidated all state on enhanced calls. The capture
does not establish which enhanced barrier covers a candidate resource.

The new component keeps enhanced buffer scopes separate from legacy states.
It requires an observed full-buffer barrier in the same command recording,
constant-compatible AccessAfter, and completion of matching split pairs.
Reset, destruction, legacy replacement, global/unknown barriers and incomplete
or mismatched splits discard eligibility. Texture barriers do not establish
buffer eligibility. Device support and command-list interface 7 are required.

AccessAfter is not treated as a permanent resource state or complete access
history. Before the bounded copy, an enhanced buffer barrier synchronizes ALL
to COPY and makes COMMON access visible to COPY_SOURCE. After the copy, a
COPY-to-ALL barrier returns access to COMMON. No buffer layout transition or
legacy ResourceBarrier is injected. COMMON-before deliberately flushes all
possible preceding writes and can be expensive; this is a six-copy research
capture, not a per-frame rendering strategy. Microsoft documents a performance
warning for COMMON-before; debug errors/corruption still fail qualification.

The component test passes on local WARP and physical GPU, copying 2816 authored
bytes through DEFAULT, rejecting a pending split and a blocked fence, and
independently verifying unchanged source data. Metadata controls reject missing
and mismatched split endings, partial ranges, mixed-family confusion, forgotten
resources and global invalidation. All four optional tests pass in Debug and
Release. Local debug layer is unavailable. Observer integration and actual-game
enhanced-buffer acquisition remain unverified at this stage.

Primary reference: [Microsoft enhanced barriers specification](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html),
including buffer access scopes, COMMON access, split pairs and legacy interop.
