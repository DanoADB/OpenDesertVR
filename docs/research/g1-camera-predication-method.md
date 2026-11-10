# Terrain camera-input capture and predication

The live29/30 controls establish displayed influence and opposite clip-X
displacement for the exact observed terrain vertex shader C48DB423, 25,924
bytes. They do not establish a complete camera adapter. The next question is
which bytes of b18/space35 this vertex stage consumes, before considering a
consistent world-view transformation.

The observed terrain binding is 2,816 bytes. Initial32 collection selects only
this observed vertex hash and this exact slot/space/size. It records bounded
static load metadata and, only where native lifetime/state proof permits, a
fenced private readback. It does not use the historical engine-camera layout,
write game memory, modify original buffers, or claim camera semantics from a
plausible matrix. Preserve metadata when a copy is rejected; do not relax the
guard or expand to arbitrary memory ranges.

## Why the capture gate changed

Microsoft documents that [predication](https://learn.microsoft.com/en-us/windows/win32/direct3d12/predication)
affects CopyBufferRegion but not query begin/end/resolution. Therefore the
earlier fenced GPU-work query evidence remains separate from constant-copy
validity. A fence completing after a skipped copy does not establish valid
source words. Previous owned constant-copy qualifications used unpredicated
recordings and did not establish this missing condition in an arbitrary game
recording. No earlier game camera claim is promoted by this fix.

Native draw contract v2 uses a new GUID, size56 and capability mask15. The
wrapper reports SetPredication after forwarding the exact native call. Direct
recordings start disabled, as specified by
[SetPredication](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setpredication).
The state keeps presence only; it neither retains nor reads the predicate
buffer. Any bound predicate rejects capture, including one that happens to
permit drawing. Reset restores disabled state; invalidation loses proof.
Render-pass/suspension and unknown/protected state still reject copies.

Both UPLOAD and DEFAULT copies now require valid v2 native proof. Turning off
metadata cannot bypass this gate: without the native draw sink, copies reject
as unverified. Exact single forwarding and its GPU-work queries retain the
application's predicate. Replay eligibility additionally requires an unbound
predicate. No application predicate is disabled or rebound for observation.

## Qualification and limits

[Exact32 qualification](../evidence/g1-owned-camera-predication32-2026-11-10.json)
uses independent frozen native/addon sources and the modern compiler pair.
Owned fixtures exercise both heap paths through unbound, zero-predicate skip,
zero-predicate allow, and unbound recovery. Pixel checks distinguish actual
skipped/allowed application draws. Bound-predicate captures export no payload;
cleared predicates permit exact source words. Unknown-size root CBVs remain
unread, and unsubmitted copies never become READY. An older native wrapper
cannot grant v2 proof. Metadata-disabled capture also rejects copies.

The same binaries separately pass static access/CBV layout and word controls,
native render-pass rejection, legacy/stream clip/query controls, and the
idle-Present capture-window regression. Debug and Release each pass all17
owned CTests. Owned apps exit cooperatively with zero native-device reference
warnings and no exported assets. Local D3D12 debug-layer validation is
unavailable; hosted validation and game-specific observation remain distinct.

The first live32 capture must run effect-disabled in a stationary loaded scene,
verify the exact game/tool/staged identities, select one D3D12 device and use a
finite bridge request budget. Retain only bounded numeric metadata and private
constant samples, with original rendering untouched. Inspect vertex-stage
load bounds before assigning any camera field meaning. GPU readback completion
does not establish the engine's CPU camera publication contract, writer
ownership, whole-world coverage, stereo history or additional world rendering.
Live32 capture, game stereo, head tracking and game OpenXR delivery are NOT_RUN.
