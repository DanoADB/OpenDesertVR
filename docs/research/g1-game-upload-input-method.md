# Bounded game candidate upload inputs

The current game's selected finite 2816-byte bindings reference DEFAULT
65536-byte allocations. The UPLOAD-only direct reader rejects them; their
current resource states are unknown. Do not guess states or transition them.
See [actual heap evidence](../evidence/g1-game-source-heaps-2026-11-06.json).

Use separately pinned `bc0ead3-upload-input-7`, input lock
`experiments/renodx-owned/devkit-upload-swapchain-input-lock.json` and its
combined patch. Actual owned region, whole-buffer and empty-reset controls
pass with exact input values, unchanged independent pixels, normal exit,
postflight checks and zero native device reference warnings. Earlier v6
shutdown failed and is excluded. See [qualification](../evidence/g1-owned-upload-inputs-2026-11-06.json).

Retain the reviewed Steam executable fingerprint, exclusive two-file bootstrap,
normal Steam launch, exact process creation-time/image/pipe checks, empty
shader directories, bounded RPCs and exact-hash cleanup from the
[constant observation method](g1-bounded-game-constants-method.md).
Never replace an addon in a running process or close unsaved gameplay.

In a fresh case explicitly set `ODVConstantBytes=2816` and `ODVUploadInputs=1`.
At most six finite active DEFAULT ranges are registered from the retained
draw/dispatch records. For five seconds after registration, a subsequent app
CopyBufferRegion or equal-sized whole-buffer CopyResource can match only if
it fully covers a candidate and its source is valid UPLOAD memory. Partial
updates are not assembled. GPU-only sources and other paths stay unavailable.
Strong references preserve identity; size alone does not identify a camera.

At the matching list's execution callback, an exclusively owned copy list is
submitted on that actual app queue, after its preceding waits and before the
upcoming app list. Its own post-submission fence gates readback. The app copy
is preserved; neither app source nor destination is written or transitioned
by the observer. Empty-reset/Close invalidates an unsubmitted match. At most
six unresolved copies share the fixed capacity. Retained target/source
references prolong existing allocations; this does not bound upstream caches
or total process memory. Unknown GPU completion stays quarantined.

Take at most three separately labeled snapshots: title menu, user-loaded
static world and deliberate camera movement. Retain at most 128 records each
and query four summary pages plus six details, selecting nonzero
`odvUploadInputCount` rows first. Wait past the five-second match window while
checking the same process remains alive before collecting final ticket states.
Never overwrite observations or restart solely because a read timed out.

These samples are **subsequent upload inputs**, not the destination's later
GPU state or the exact contents at the originally observed draw. Other GPU
writers can change the destination afterward. Keep arrays/handles/logs private;
publish only fingerprints, counts, sizes, states and interpretation. No shader
text, game assets, replacements, camera writes or stereo claims follow from
returned values. World-scene and camera-change evidence are still required.
Finish with normal exit and removal of only the exact two created files.
