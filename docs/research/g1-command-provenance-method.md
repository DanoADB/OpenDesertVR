# Command provenance before camera selection

The pinned [ReShade D3D12 wrapper](https://github.com/crosire/reshade/blob/4a50d1eddace85734871d91792ff214f13f66c01/source/d3d12/d3d12_command_list.cpp#L845)
reports ExecuteIndirect through an unknown-kind callback; it does not expose
the signature layout there. RenoDX retained graphics and compute state in
separate domains. Its older fallback tested whether a cached compute shader
existed, which can survive a later graphics PSO bind.

An actual owned compute-then-indirect-draw case reproduced that error while
the GPU still rendered the correct RGB/black pixels. The corrected observer
uses the latest D3D12 PSO bind point, reports API-command and binding-phase
provenance, and keeps unknown domains unsampled. Known indirect graphics is
not proof of an indexed signature or of bindings after argument execution.
See [before/after runtime evidence](../evidence/g1-owned-command-provenance-2026-11-06.json).

The first corrected game case used `bc0ead3-command-provenance-8` and returned
one 768-byte subsequent UPLOAD input at the title menu. Its normal exit and
exact staging cleanup passed. Camera identity remains unverified.

The same six details contain two unique active 2816-byte ranges. One occurs
in six records at b14/b17, space35; the other occurs in five at b18/b21,
space35. Both also occur on the retained direct indexed draw, not only in
pre-indirect bindings. These are shared-data candidates to compare during
world-camera movement, not identified camera buffers. Both returned no upload
input within the menu observation window; the ready 768-byte range is different.
See [derived binding evidence](../evidence/g1-game-shared-constant-candidates-2026-11-06.json).

For the next game case use separately pinned `bc0ead3-deferred-release-9`,
its input lock and combined patch. Retain all process identity, reviewed build,
exclusive bootstrap staging, bounded RPC, no-shader-export and exact cleanup
requirements from [the upload method](g1-game-upload-input-method.md).
No in-place addon replacement or unsaved-game interruption is allowed.

The owned transient-allocation control timed out during a detail read with
observer8. Final resource Release can reenter resource-destruction callbacks
that acquire device-data locks. Observer9 transfers retired resource references
to a bounded 64-slot queue without Release under capture locks. Drain before
OnPresent takes device-data locks and after early primary-swapchain retirement,
while the device is alive. Queue overflow retains references and applies
backpressure. Completion and command-recording retirement remain prerequisites.
Actual owned controls passed for temporary upload sources, replacement DEFAULT
targets followed by a second snapshot, and direct UPLOAD sampling. All passed
unchanged GPU pixels, normal exits and zero native device-reference warnings.
See [before/after evidence](../evidence/g1-owned-transient-release-2026-11-06.json).

Explicitly set `ODVGraphicsOnly=1`, `ODVConstantBytes=0`,
`ODVConstantMaxBytes=4096`, `ODVUploadInputs=1`. This retains at most 128 known
graphics records and considers actual finite active CBV ranges up to 4 KiB,
rather than guessing that 2816 bytes identifies a camera. Unknown/full ranges
remain excluded. Six copy slots, six DEFAULT candidates and six attempts per
record remain; overflow/truncation is not evidence of missing world work.
Both direct UPLOAD acquisition and covering upload-input acquisition have
owned runtime controls, including mixed-size bounds and unchanged pixels.

Keep at most three non-overwritten observations (menu, static world, camera
movement), four summary pages and six details per snapshot. Select originating
sample/input tickets first, then distinct graphics shader pairs. Wait past the
five-second upload window while validating the same process. Preserve API
origin, classification-known, pre-indirect binding phase and filter settings.
Legacy game Dispatch counts are reported labels, not verified command kinds.

Returned arrays stay private. Direct samples and subsequent upload inputs are
different evidence; neither establishes a world camera without scene/movement
correlation. This mode adds no app buffer writes, transitions, shader changes,
argument-buffer decoding or stereo claim. Stop the observation on identity or
callback-contract failures; a timeout alone does not authorize a restart.
